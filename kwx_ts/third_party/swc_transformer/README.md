# swc_transformer

The TypeScript-to-CommonJS transformer behind `kwx_ts`.

`kwx_ts` used to embed Sucrase as a JavaScript bundle, compile it to QuickJS
bytecode, and evaluate it before the first `.ts` module could load. This crate
replaces that: the transform is native code, linked into the executable as a
`staticlib`, so there is no transpiler to evaluate and nothing to keep in sync
between a JS bundle and the host.

## What it does

Given one TypeScript module, it produces the CommonJS source the host compiles
through `__kwx_eval_factory(source, filePath)` with the real module path, which
is what makes stack frames name the file. The pipeline is:

1. parse with `Syntax::Typescript` (JSX off, decorators on);
2. `resolver` — assign syntax contexts so type-only imports can be elided;
3. `typescript::strip` — remove types, lower `enum` / `namespace` / `const enum`;
4. `decorators` — legacy `experimentalDecorators`, `emit_metadata: false`;
5. `common_js` — ESM to `require` / `exports`, `ImportInterop::Swc`;
6. `inject_helpers` — inline the helpers the passes above enabled;
7. `fixer`;
8. codegen at `EsVersion::latest()` (nothing is downlevelled).

The emitted buffer ends with a single line
`//# sourceMappingURL=data:application/json;base64,<data>`. The map is produced
by `SourceMap::build_source_map` with `DefaultSourceMapGenConfig`, which requires
the `swc_core` `common_sourcemap` feature; `sources[0]` is the `file_name` string
passed to `kwx_swc_transform`, so the host can map a frame back to the module
path and original position.

Two details matter for parity with the previous Sucrase pipeline:

- `ecma_helpers_inline` is enabled and `Helpers::new(false)` is used, so helper
  bodies (`_ts_decorate`, `_interop_require_default`, ...) are emitted into the
  module. The output must never contain `require("@swc/helpers/...")`: there is
  no module resolution for bare specifiers other than the two embedded ones.
- The program is parsed and emitted at the latest ES version, which keeps class
  fields, optional chaining and the like untouched — the equivalent of Sucrase's
  `disableESTransforms: true`.

## C ABI

The library exports four functions (`extern "C"`, no C++ ABI dependency):

```c
const char *kwx_swc_version(void);
char       *kwx_swc_transform(const char *source, const char *file_name,
                              size_t *out_len);
const char *kwx_swc_last_error(void);
void        kwx_swc_free(char *ptr);
```

- `kwx_swc_transform` returns a NUL-terminated buffer of `*out_len` bytes, or
  NULL on failure. The buffer is the CommonJS code, one blank line, then the
  `//# sourceMappingURL=data:application/json;base64,<map>` line. The caller owns
  the buffer and releases it with `kwx_swc_free`.
- Appending the map changed the output without changing the dependency versions,
  so `CACHE_ABI` in `src/lib.rs` was bumped to `"3"`. Cached modules written by
  an older pipeline are therefore never reused.
- `kwx_swc_last_error` describes the last failure on the calling thread. It is
  overwritten by the next call on that thread, which matches the
  single-threaded QuickJS host.
- `kwx_swc_version` is stable for a build and includes the `swc_core` version
  and a pipeline revision (`CACHE_ABI` in `src/lib.rs`). `runtime_bindings.cpp`
  mixes it into the transpile-cache key, so a new engine or a changed pipeline
  invalidates every cached module.

Panics are caught at the FFI boundary and reported as an error string; they
never unwind into C++. `panic = "unwind"` is required for that and is set in
`Cargo.toml`.

## Building

Cargo builds this crate as part of the normal `kwx_ts` build; see the
`TypeScript transformer (Rust / SWC)` section of the repository root
`CMakeLists.txt`. It is built with `--release` regardless of the executable's
configuration, because the transformer runs on every module load.

The Rust target (`x86_64-pc-windows-msvc`, `x86_64-pc-windows-gnu`, ...) is
derived from the C++ toolchain, not from the host: an MSVC-linked `kwx_ts`
cannot link a MinGW archive or vice versa. macOS Universal builds compile one
slice per architecture and merge them with `lipo`.

To build and test the crate on its own:

```
cd kwx_ts/third_party/swc_transformer
cargo test --release
```

The tests cover type stripping, enum/namespace lowering, legacy decorators
(including that `_ts_decorate` is defined inline), CommonJS interop without
`@swc/helpers`, syntax-error reporting, and the appended inline source map
(marker present, base64-decoded JSON is version 3 with the input file name and
non-empty mappings that span more than one generated line).

## Upgrading SWC

1. Change the pinned version in `Cargo.toml` (`swc_core = "=x.y.z"`).
2. Run `cargo update` here and commit the new `Cargo.lock`.
3. Run `cargo test --release`, then rebuild `kwx_ts` and run
   `bin\Release\kwxts.exe kwx_ts\tests\smoke.ts`.

The version string changes with the `swc_core` version, so old cache entries
become unreachable automatically; there is no separate cache-version bump to
forget. `CACHE_ABI` in `src/lib.rs` is the knob for changes to *this* pipeline
that alter output without changing the dependency versions.
