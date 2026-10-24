//! kwx_ts — TypeScript → CommonJS transformer.
//!
//! This crate is the transpilation engine behind `kwxTranspile()` in
//! `runtime/host_runtime.js`. It replaces the Sucrase bundle that used to be
//! compiled to QuickJS bytecode: the TypeScript is now lowered natively, so the
//! runtime no longer pays to evaluate a 40 000-line JavaScript parser before the
//! first module can be loaded.
//!
//! The pipeline intentionally mirrors what Sucrase's `typescript` + `imports`
//! transforms produced, plus the syntax Sucrase never supported (namespaces,
//! `const enum`, legacy decorators):
//!
//!   1. parse with the TypeScript syntax (JSX off, decorators on)
//!   2. `resolver` — assign syntax contexts so the later passes can elide
//!      type-only imports correctly
//!   3. `typescript::strip` — drop types, lower enums/namespaces
//!   4. `decorators` (legacy) — what `experimentalDecorators` expects
//!   5. `common_js` — ESM → CommonJS, inlined interop helpers
//!   6. `fixer` — parenthesise where precedence requires it
//!   7. codegen at `EsVersion::latest()`, so no ES feature is downlevelled
//!      (`disableESTransforms` in Sucrase terms)
//!
//! # ABI
//!
//! The crate is built as a `staticlib` and linked straight into `kwx_ts`, so
//! there is no DLL to ship and no loader to configure:
//!
//! ```c
//! const char *kwx_swc_version(void);
//! char       *kwx_swc_transform(const char *source, const char *file_name,
//!                               size_t *out_len);
//! const char *kwx_swc_last_error(void);
//! void        kwx_swc_free(char *ptr);
//! ```
//!
//! `kwx_swc_transform` returns a NUL-terminated buffer the caller must release
//! with `kwx_swc_free`, or NULL when the module does not parse; in that case
//! `kwx_swc_last_error()` describes the failure. The buffer holds the generated
//! CommonJS code followed by one blank line and one
//! `//# sourceMappingURL=data:application/json;base64,<map>` line, so the source
//! map travels inside the payload and is cached with it. Both the message and
//! the buffer are thread-local, which matches the single-threaded QuickJS host.

use std::cell::RefCell;
use std::ffi::{c_char, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;
use std::sync::OnceLock;

use swc_core::common::source_map::DefaultSourceMapGenConfig;
use swc_core::common::{
    sync::Lrc, BytePos, FileName, Globals, LineCol, Mark, SourceMap, Spanned, GLOBALS,
};
use swc_core::ecma::ast::{EsVersion, Program};
use swc_core::ecma::codegen::{text_writer::JsWriter, Config as CodegenConfig, Emitter};
use swc_core::ecma::parser::{lexer::Lexer, Parser, StringInput, Syntax, TsSyntax};
use swc_core::ecma::transforms::base::helpers::{inject_helpers, Helpers, HELPERS};
use swc_core::ecma::transforms::base::{fixer::fixer, resolver};
use swc_core::ecma::transforms::module::common_js::{common_js as common_js_pass, FeatureFlag};
use swc_core::ecma::transforms::module::path::Resolver;
use swc_core::ecma::transforms::module::util::{Config as CjsConfig, ImportInterop};
use swc_core::ecma::transforms::proposal::decorators::{self, Config as DecoratorConfig};
use swc_core::ecma::transforms::typescript::strip;

/// Version of the *transpile semantics* below. Folded into the version string,
/// which the C++ side mixes into the transpile-cache key, so any change in the
/// pipeline invalidates previously cached modules.
const CACHE_ABI: &str = "3";

fn version_string() -> String {
    format!(
        "kwx-swc/{} (swc_core {}, transpile-abi {})",
        env!("CARGO_PKG_VERSION"),
        swc_core::SWC_CORE_VERSION.trim(),
        CACHE_ABI
    )
}

/// Encodes `input` as standard base64 (RFC 4648 alphabet, `=` padding, no
/// line breaks) for the inline `//# sourceMappingURL` payload.
///
/// A local encoder keeps the crate free of a dependency for ~20 lines of code;
/// the output is consumed by the host's JavaScript decoder, which expects the
/// canonical alphabet and padding.
fn kwx_base64_encode(input: &[u8]) -> String {
    const ALPHABET: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    let mut encoded = String::with_capacity(input.len().div_ceil(3) * 4);
    for chunk in input.chunks(3) {
        let byte0 = chunk[0] as u32;
        let byte1 = chunk.get(1).copied().unwrap_or(0) as u32;
        let byte2 = chunk.get(2).copied().unwrap_or(0) as u32;
        let triple = (byte0 << 16) | (byte1 << 8) | byte2;

        encoded.push(ALPHABET[(triple >> 18) as usize & 0x3f] as char);
        encoded.push(ALPHABET[(triple >> 12) as usize & 0x3f] as char);
        if chunk.len() > 1 {
            encoded.push(ALPHABET[(triple >> 6) as usize & 0x3f] as char);
        } else {
            encoded.push('=');
        }
        if chunk.len() > 2 {
            encoded.push(ALPHABET[triple as usize & 0x3f] as char);
        } else {
            encoded.push('=');
        }
    }

    encoded
}

fn transform_impl(source: &str, file_name: &str) -> Result<Vec<u8>, String> {
    let source_map: Lrc<SourceMap> = Default::default();
    let source_file = source_map.new_source_file(
        Lrc::new(FileName::Custom(file_name.to_string())),
        source.to_string(),
    );

    // `into_flags()` in the lexer already enables the TS-specific extras (auto
    // accessors, decorators-before-export, import attributes); only the switch
    // that differs from Sucrase's behaviour is set explicitly.
    let syntax = Syntax::Typescript(TsSyntax {
        tsx: false,
        decorators: true,
        ..Default::default()
    });

    let lexer = Lexer::new(
        syntax,
        EsVersion::latest(),
        StringInput::from(&*source_file),
        None,
    );

    let mut parser = Parser::new_from(lexer);
    let mut program: Program = parser.parse_program().map_err(|error| {
        let line = source_map.lookup_char_pos(error.span().lo).line;
        format!("{file_name}: {} (line {line})", error.kind().msg())
    })?;

    // Span hygiene data (`Mark`s) lives in a scoped thread-local; the passes
    // below allocate marks and panic without it.
    GLOBALS.set(&Globals::default(), || {
        // Helper injection (`_interop_require_default`, `_define_property`, ...)
        // is a scoped thread-local too. `false` means "inline the helper body
        // into the module" rather than emit `require("@swc/helpers/...")`.
        HELPERS.set(&Helpers::new(false), || {
            run_passes(&mut program, &source_map, file_name)
        })
    })
}

/// Applies the transform pipeline to an already parsed program.
///
/// Must run inside the `GLOBALS` and `HELPERS` scopes established by
/// [`transform_impl`]: the passes allocate `Mark`s and inject helpers.
fn run_passes(
    program: &mut Program,
    source_map: &Lrc<SourceMap>,
    file_name: &str,
) -> Result<Vec<u8>, String> {
    let unresolved_mark = Mark::new();
    let top_level_mark = Mark::new();

    program.mutate(resolver(unresolved_mark, top_level_mark, true));
    program.mutate(strip(unresolved_mark, top_level_mark));

    // Legacy `experimentalDecorators`. `emit_metadata` stays off so no
    // `Reflect.metadata` / `design:type` calls are emitted (the QuickJS host
    // provides no such runtime), and class fields use [[Define]] semantics to
    // match native class fields, exactly as Sucrase left them.
    program.mutate(decorators::decorators(DecoratorConfig {
        legacy: true,
        emit_metadata: false,
        use_define_for_class_fields: true,
    }));

    // Sucrase's `imports` transform. `ImportInterop::Swc` produces the
    // `_interop_require_default` / `_interop_require_wildcard` shape the
    // generated bindings rely on; the helpers are inlined into each module
    // (`ecma_helpers_inline`), so the output never requires `@swc/helpers`.
    program.mutate(common_js_pass(
        Resolver::Default,
        unresolved_mark,
        CjsConfig {
            strict_mode: true,
            import_interop: Some(ImportInterop::Swc),
            ..Default::default()
        },
        FeatureFlag {
            support_block_scoping: true,
            support_arrow: true,
        },
    ));

    // Prepend the definitions of the helpers the passes above marked as used
    // (`_ts_decorate`, `_interop_require_default`, ...). With the
    // `inline-helpers` feature and `Helpers::new(false)` these are emitted as
    // plain functions in the module, so nothing is required at runtime.
    program.mutate(inject_helpers(unresolved_mark));

    program.mutate(fixer(None));

    let mut output = Vec::new();
    let mut src_map_buf: Vec<(BytePos, LineCol)> = Vec::new();
    {
        let writer = JsWriter::new(
            source_map.clone(),
            "\n",
            &mut output,
            Some(&mut src_map_buf),
        );
        let mut emitter = Emitter {
            cfg: CodegenConfig::default().with_target(EsVersion::latest()),
            cm: source_map.clone(),
            comments: None,
            wr: writer,
        };
        emitter
            .emit_program(program)
            .map_err(|error| format!("{file_name}: code generation failed: {error}"))?;
    }

    // An inline v3 map rides along with the code: the host stores the whole
    // buffer in the transpile cache and decodes the map when an error needs a
    // stack trace, so no side file or extra cache field is required.
    let generated = source_map.build_source_map(&src_map_buf, None, DefaultSourceMapGenConfig);
    let mut map_json = Vec::new();
    generated
        .to_writer(&mut map_json)
        .map_err(|error| format!("{file_name}: source map serialization failed: {error}"))?;
    output.extend_from_slice(b"\n//# sourceMappingURL=data:application/json;base64,");
    output.extend_from_slice(kwx_base64_encode(&map_json).as_bytes());
    output.push(b'\n');

    Ok(output)
}

thread_local! {
    /// Describes the most recent failure on this thread. Kept in a `CString` so
    /// the pointer handed to C stays valid until the next call.
    static LAST_ERROR: RefCell<Option<CString>> = const { RefCell::new(None) };
}

fn set_last_error(message: String) {
    // A NUL inside the message would truncate the C string; replace it.
    let sanitized = message.replace('\0', " ");
    LAST_ERROR.with(|slot| {
        *slot.borrow_mut() = CString::new(sanitized).ok();
    });
}

/// Version of the transformer and of the SWC engine it embeds.
///
/// The string is stable for a given build and is mixed into the transpile-cache
/// key, so rebuilding against a different `swc_core` invalidates every cached
/// entry instead of silently serving output from another engine.
#[no_mangle]
pub extern "C" fn kwx_swc_version() -> *const c_char {
    static VERSION: OnceLock<CString> = OnceLock::new();
    VERSION
        .get_or_init(|| CString::new(version_string()).unwrap_or_default())
        .as_ptr()
}

/// Last failure recorded by [`kwx_swc_transform`] on this thread, or an empty
/// string when the previous call succeeded.
#[no_mangle]
pub extern "C" fn kwx_swc_last_error() -> *const c_char {
    static EMPTY: OnceLock<CString> = OnceLock::new();
    let empty = EMPTY.get_or_init(|| CString::new("").expect("empty string is a valid CString"));

    LAST_ERROR.with(|slot| match slot.borrow().as_ref() {
        Some(message) => message.as_ptr(),
        None => empty.as_ptr(),
    })
}

/// Transpiles one TypeScript module to CommonJS.
///
/// Returns a NUL-terminated UTF-8 buffer of `*out_len` bytes (the terminator is
/// not counted) that the caller owns and must release with [`kwx_swc_free`].
/// Returns NULL on failure; call [`kwx_swc_last_error`] for the reason.
///
/// # Safety
///
/// `source` and `file_name` must be valid NUL-terminated strings. `out_len` may
/// be NULL, otherwise it must point to writable storage for one `size_t`.
#[no_mangle]
pub unsafe extern "C" fn kwx_swc_transform(
    source: *const c_char,
    file_name: *const c_char,
    out_len: *mut usize,
) -> *mut c_char {
    if !out_len.is_null() {
        // SAFETY: the caller guarantees `out_len` is writable.
        unsafe { *out_len = 0 };
    }

    if source.is_null() || file_name.is_null() {
        set_last_error("kwx_swc_transform: NULL source or file name".to_string());
        return ptr::null_mut();
    }

    // SAFETY: both pointers are NUL-terminated per the contract above.
    let source = match unsafe { CStr::from_ptr(source) }.to_str() {
        Ok(text) => text,
        Err(error) => {
            set_last_error(format!("kwx_swc_transform: source is not UTF-8: {error}"));
            return ptr::null_mut();
        }
    };
    // SAFETY: as above.
    let file_name = match unsafe { CStr::from_ptr(file_name) }.to_str() {
        Ok(text) => text,
        Err(error) => {
            set_last_error(format!("kwx_swc_transform: file name is not UTF-8: {error}"));
            return ptr::null_mut();
        }
    };

    // A panic must never unwind into the C++ caller. Rust's default panic hook
    // still prints the message; the returned NULL is what the host reports.
    let result = catch_unwind(AssertUnwindSafe(|| transform_impl(source, file_name)));

    let mut output = match result {
        Ok(Ok(output)) => output,
        Ok(Err(message)) => {
            set_last_error(message);
            return ptr::null_mut();
        }
        Err(payload) => {
            let reason = payload
                .downcast_ref::<&str>()
                .map(|text| (*text).to_string())
                .or_else(|| payload.downcast_ref::<String>().cloned())
                .unwrap_or_else(|| "unknown panic".to_string());
            set_last_error(format!("kwx_swc_transform: internal error: {reason}"));
            return ptr::null_mut();
        }
    };

    // The returned C string must not contain interior NULs. A JavaScript module
    // produced by the code generator cannot, but a NUL in the source could
    // reach a string literal verbatim; replace it rather than fail the module.
    if output.contains(&0) {
        for byte in output.iter_mut() {
            if *byte == 0 {
                *byte = b' ';
            }
        }
    }

    LAST_ERROR.with(|slot| {
        *slot.borrow_mut() = None;
    });

    match CString::new(output) {
        Ok(text) => {
            if !out_len.is_null() {
                // SAFETY: the caller guarantees `out_len` is writable.
                unsafe { *out_len = text.as_bytes().len() };
            }
            text.into_raw()
        }
        Err(error) => {
            set_last_error(format!("kwx_swc_transform: {error}"));
            ptr::null_mut()
        }
    }
}

/// Releases a buffer returned by [`kwx_swc_transform`].
///
/// # Safety
///
/// `ptr` must be NULL or a pointer previously returned by
/// [`kwx_swc_transform`] that has not been freed yet.
#[no_mangle]
pub unsafe extern "C" fn kwx_swc_free(ptr: *mut c_char) {
    if ptr.is_null() {
        return;
    }
    // SAFETY: the pointer came from `CString::into_raw` in this crate.
    drop(unsafe { CString::from_raw(ptr) });
}

#[cfg(test)]
mod tests {
    use super::*;

    fn transform(source: &str) -> String {
        String::from_utf8(transform_impl(source, "test.ts").expect("transpile failed"))
            .expect("output is not UTF-8")
    }

    const MAP_MARKER: &str = "//# sourceMappingURL=data:application/json;base64,";

    /// Reverses [`kwx_base64_encode`] for the test assertions. Padding and
    /// non-alphabet characters are ignored, which is enough for our own output.
    fn base64_decode(text: &str) -> Vec<u8> {
        fn value_of(byte: u8) -> Option<u32> {
            match byte {
                b'A'..=b'Z' => Some((byte - b'A') as u32),
                b'a'..=b'z' => Some((byte - b'a') as u32 + 26),
                b'0'..=b'9' => Some((byte - b'0') as u32 + 52),
                b'+' => Some(62),
                b'/' => Some(63),
                _ => None,
            }
        }

        let mut accumulator = 0u32;
        let mut pending_bits = 0u32;
        let mut decoded = Vec::new();
        for &byte in text.as_bytes() {
            let Some(value) = value_of(byte) else {
                continue;
            };
            accumulator = (accumulator << 6) | value;
            pending_bits += 6;
            if pending_bits >= 8 {
                pending_bits -= 8;
                decoded.push((accumulator >> pending_bits) as u8);
                accumulator &= (1 << pending_bits) - 1;
            }
        }
        decoded
    }

    /// Returns the decoded v3 map JSON carried by `code`.
    fn inline_source_map_json(code: &str) -> String {
        let start = code
            .find(MAP_MARKER)
            .expect("output carries no inline source map")
            + MAP_MARKER.len();
        let rest = &code[start..];
        let end = rest.find('\n').unwrap_or(rest.len());
        String::from_utf8(base64_decode(&rest[..end])).expect("decoded map is not UTF-8")
    }

    /// Reads the `mappings` string out of the map JSON. The value is VLQ text,
    /// so it never contains a quote and a plain scan is sufficient.
    fn source_map_mappings(map_json: &str) -> String {
        const KEY: &str = "\"mappings\":\"";
        let start = map_json.find(KEY).expect("map has no mappings field") + KEY.len();
        let rest = &map_json[start..];
        let end = rest.find('"').expect("unterminated mappings value");
        rest[..end].to_string()
    }

    #[test]
    fn strips_types() {
        let code = transform("const value: number = 1;\nexport { value };\n");
        assert!(!code.contains(": number"), "{code}");
        assert!(code.contains("Object.defineProperty(exports, \"value\""), "{code}");
    }

    #[test]
    fn lowers_enum_and_namespace() {
        let code = transform(
            "export enum Colour { Red, Green }\nexport namespace N { export const x = 1; }\n",
        );
        assert!(code.contains("Colour"), "{code}");
        assert!(!code.contains("namespace"), "{code}");
    }

    #[test]
    fn lowers_legacy_decorators_without_metadata() {
        let code = transform(
            "function dec(target: any) { return target; }\n\
             @dec\n\
             export class Point { x: number = 0; }\n",
        );
        assert!(!code.contains("@dec"), "{code}");
        assert!(!code.contains("Reflect.metadata"), "{code}");
        assert!(!code.contains("design:type"), "{code}");
        // The legacy transform emits `_ts_decorate(...)`; the helper must be
        // defined in the module rather than required from `@swc/helpers`.
        assert!(code.contains("function _ts_decorate"), "{code}");
        assert!(!code.contains("@swc/helpers"), "{code}");
    }

    #[test]
    fn emits_commonjs_without_external_helpers() {
        let code =
            transform("import path from \"path\";\nexport const base = path.basename(\"/a/b\");\n");
        assert!(code.contains("require(\"path\")"), "{code}");
        assert!(!code.contains("@swc/helpers"), "{code}");
    }

    #[test]
    fn reports_syntax_errors() {
        let error = transform_impl("const x: = 1;", "bad.ts").expect_err("should not parse");
        assert!(error.contains("bad.ts"), "{error}");
    }

    #[test]
    fn version_is_stable() {
        assert!(version_string().starts_with("kwx-swc/"));
    }

    #[test]
    fn emits_inline_source_map() {
        let code = transform("export const value: number = 1;\n");

        assert!(code.contains(MAP_MARKER), "{code}");
        assert_eq!(code.matches(MAP_MARKER).count(), 1, "{code}");
        assert!(code.ends_with('\n'), "{code}");

        let map_json = inline_source_map_json(&code);
        assert!(map_json.contains("\"version\":3"), "{map_json}");
        assert!(map_json.contains("\"test.ts\""), "{map_json}");
        assert!(!source_map_mappings(&map_json).is_empty(), "{map_json}");
    }

    #[test]
    fn inline_source_map_maps_statements() {
        let code = transform(
            "const first: number = 1;\nconst second: number = 2;\nconst third: number = 3;\n",
        );

        let mappings = source_map_mappings(&inline_source_map_json(&code));
        // A three-statement module spans more than one generated line, which the
        // map expresses as `;` separators between line groups.
        assert!(mappings.contains(';'), "{mappings}");
    }
}
