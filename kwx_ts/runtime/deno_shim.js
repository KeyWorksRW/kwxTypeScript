// kwx_ts — Deno compatibility shim (Phase 4/5).
//
// Exposes the small, deliberately-bounded slice of the Deno API that the
// existing kwxFFI binding corpus uses, plus `console` and `TextEncoder`, and a
// CommonJS-style module loader that transpiles TypeScript with the embedded
// Sucrase bundle (Phase 5).
//
// Everything here runs on top of the `__kwx_native` object: `Deno.dlopen` never
// touches a real dynamic library, it resolves every requested symbol directly
// from the statically linked kwxFFI surface. The Deno surface is intentionally
// isolated in this file so it can be replaced without touching native code.
//
// Supported Deno surface (exactly what the corpus needs):
//   Deno.build.os, Deno.args, Deno.exit, Deno.dlopen,
//   Deno.UnsafePointer.of, Deno.UnsafePointerView.getCString,
//   Deno.UnsafeCallback.
//
// Module resolution (Phase 5): relative/absolute specifiers resolve to files on
// disk; the bare specifiers "kwx", "kwxTypeScript", and "@kwx" resolve to the
// embedded kwx_runtime asset, so importing the binding corpus never reads from
// disk.

(function () {
    "use strict";

    var native = globalThis.__kwx_native;
    if (!native) {
        throw new Error("kwx_ts: __kwx_native is not available");
    }

    // ------------------------------------------------------------ trace marks
    // Opt-in startup timing: __kwx_trace is a cheap no-op unless KWX_TS_TRACE is
    // set, so no JS-side enabled check is needed.
    function kwxTrace(label) {
        var trace = native.__kwx_trace;
        if (typeof trace === "function") {
            trace(label);
        }
    }

    // ---------------------------------------------------------------- console
    // stdout for log/info/debug, stderr for warn/error.
    function kwxMakeConsoleWriter(stream) {
        return function () {
            var parts = [];
            for (var index = 0; index < arguments.length; ++index) {
                parts.push(String(arguments[index]));
            }
            native.__kwx_print(stream, parts.join(" "));
        };
    }

    globalThis.console = {
        log: kwxMakeConsoleWriter(0),
        info: kwxMakeConsoleWriter(0),
        debug: kwxMakeConsoleWriter(0),
        warn: kwxMakeConsoleWriter(2),
        error: kwxMakeConsoleWriter(2)
    };

    // ------------------------------------------------------------ TextEncoder
    function TextEncoder() {}

    TextEncoder.prototype.encode = function (input) {
        var text = (input === undefined || input === null) ? "" : String(input);
        return new Uint8Array(native.__kwx_encode_utf8(text));
    };

    globalThis.TextEncoder = TextEncoder;

    // ------------------------------------------------------------------- Deno
    // argv is [program, entry, ...userArgs]; Deno.args excludes both leading
    // entries, matching the real Deno behaviour.
    var KWX_DENO_ARG_OFFSET = 2;

    function kwxDenoDlopen(name, symbols) {
        var resolved = {};
        for (var key in symbols) {
            if (Object.prototype.hasOwnProperty.call(symbols, key)) {
                var fn = native[key];
                if (typeof fn !== "function") {
                    throw new Error(
                        "kwx_ts: Deno.dlopen('" + name + "') cannot resolve symbol '" + key + "'"
                    );
                }
                resolved[key] = fn;
            }
        }
        return {
            symbols: resolved,
            close: function () {}
        };
    }

    function kwxDenoExit(code) {
        var actual = (code === undefined || code === null) ? 0 : code;
        native.__kwx_set_exit_code(actual);
        // Thrown so the module stack unwinds; the host recognises the flag and
        // returns `actual` instead of reporting an uncaught error.
        var sentinel = new Error("kwx_ts: Deno.exit(" + actual + ")");
        sentinel.__kwx_exit = true;
        sentinel.code = actual;
        throw sentinel;
    }

    function UnsafeCallback(definition, callback) {
        this.definition = definition;
        this.callback = callback;
        this._slot = native.__kwx_callback_create(callback);
        if (this._slot < 0) {
            throw new Error("kwx_ts: Deno.UnsafeCallback: no free native callback slot");
        }
        this.pointer = native.__kwx_callback_pointer(this._slot);
        this._closed = false;
    }

    // Releases the native callback slot so it can be reused.
    UnsafeCallback.prototype.close = function () {
        if (!this._closed) {
            native.__kwx_callback_close(this._slot);
            this._closed = true;
        }
    };

    UnsafeCallback.prototype.ref = function () {};
    UnsafeCallback.prototype.unref = function () {};

    var UnsafePointer = {
        of: function (value) {
            return native.__kwx_ptr_of(value);
        }
    };

    var UnsafePointerView = {
        getCString: function (pointer) {
            return native.__kwx_get_cstring(pointer);
        }
    };

    globalThis.Deno = {
        build: {
            get os() {
                return native.__kwx_platform_os;
            }
        },
        args: native.__kwx_get_args().slice(KWX_DENO_ARG_OFFSET),
        exit: kwxDenoExit,
        dlopen: kwxDenoDlopen,
        UnsafeCallback: UnsafeCallback,
        UnsafePointer: UnsafePointer,
        UnsafePointerView: UnsafePointerView
    };

    // ---------------------------------------------------------- transpilation
    // Sucrase is embedded and loaded once by the host before any entry module
    // runs; every module that is not already plain CommonJS goes through it with
    // the `typescript` and `imports` transforms enabled.
    function kwxTranspile(source, filePath) {
        var sucrase = globalThis.__kwx_sucrase;
        if (!sucrase || typeof sucrase.transform !== "function") {
            throw new Error("kwx_ts: the embedded Sucrase bundle is not loaded");
        }
        kwxTrace("transpile-begin:" + filePath);
        var transformed = sucrase.transform(source, {
            transforms: ["typescript", "imports"],
            disableESTransforms: true,
            filePath: filePath
        }).code;
        kwxTrace("transpile-end:" + filePath);
        return transformed;
    }

    // Fast path: a .js/.cjs module with no import/export tokens is already valid
    // CommonJS, so skip Sucrase entirely. Everything else (including every .ts
    // file) is transpiled.
    function kwxNeedsTranspile(filePath, source) {
        if (!/\.(c?js)$/i.test(filePath)) {
            return true;
        }
        return /\bimport\b|\bexport\b/.test(source);
    }

    globalThis.__kwx_transpile = kwxTranspile;

    // ------------------------------------------------ CommonJS-style loader
    var kwxModuleCache = Object.create(null);

    // Bare specifiers that resolve to an embedded (in-binary) asset instead of a
    // file on disk. "kwx" is the prebuilt binding corpus; the aliases match the
    // historical module names so existing sources keep working.
    var KWX_VIRTUAL_MODULES = {
        "kwx": "kwx_runtime",
        "kwxTypeScript": "kwx_runtime",
        "@kwx": "kwx_runtime"
    };

    function kwxDirname(filePath) {
        var slash = Math.max(filePath.lastIndexOf("/"), filePath.lastIndexOf("\\"));
        return (slash > 0) ? filePath.slice(0, slash) : "";
    }

    function kwxMakeRequire(parentPath) {
        return function (specifier) {
            var asset = KWX_VIRTUAL_MODULES[specifier];
            if (asset) {
                return kwxLoadEmbedded(asset);
            }
            var resolved = native.__kwx_module_resolve(parentPath, specifier);
            if (!resolved) {
                throw new Error(
                    "kwx_ts: cannot resolve module '" + specifier + "' from '" + parentPath + "'"
                );
            }
            return kwxLoadModule(resolved);
        };
    }

    // Shared epilogue: create the module record, run a CommonJS factory, and
    // return its exports. `factory` is either built here from source or handed
    // over precompiled by the host.
    function kwxExecuteFactory(moduleId, filePath, factory) {
        var module = { id: moduleId, filename: filePath, exports: {} };
        kwxModuleCache[moduleId] = module;

        factory(module.exports, module, kwxMakeRequire(filePath), filePath, kwxDirname(filePath));
        kwxTrace("module-executed:" + moduleId);
        return module.exports;
    }

    function kwxLoadSource(moduleId, filePath, source) {
        var cached = kwxModuleCache[moduleId];
        if (cached) {
            return cached.exports;
        }

        var code = kwxCompileSource(filePath, source);
        kwxTrace("module-compiled:" + moduleId);
        var factory = new Function(
            "exports", "module", "require", "__filename", "__dirname", code
        );
        kwxTrace("module-factory-built:" + moduleId);
        return kwxExecuteFactory(moduleId, filePath, factory);
    }

    function kwxCompileSource(filePath, source) {
        if (!kwxNeedsTranspile(filePath, source)) {
            return source;
        }
        return kwxTranspile(source, filePath);
    }

    function kwxLoadModule(filePath) {
        var cached = kwxModuleCache[filePath];
        if (cached) {
            return cached.exports;
        }
        var source = native.__kwx_read_file(filePath);
        kwxTrace("module-read:" + filePath);
        return kwxLoadSource(filePath, filePath, source);
    }

    // Load a native-embedded asset as a CommonJS module. No disk access and no
    // parse: the host returns the factory from precompiled bytecode.
    function kwxLoadEmbedded(assetName) {
        var moduleId = "<embedded:" + assetName + ">";
        var cached = kwxModuleCache[moduleId];
        if (cached) {
            return cached.exports;
        }
        var factory = native.__kwx_get_embedded_factory(assetName);
        if (typeof factory !== "function") {
            throw new Error("kwx_ts: embedded asset '" + assetName + "' is unavailable");
        }
        kwxTrace("embedded-bytecode-loaded:" + assetName);
        return kwxExecuteFactory(moduleId, moduleId + ".js", factory);
    }

    globalThis.__kwx_load_module = kwxLoadModule;

    globalThis.__kwx_run_main = function (entryPath) {
        var resolved = native.__kwx_module_resolve("", entryPath);
        if (!resolved) {
            throw new Error("kwx_ts: cannot resolve entry module '" + entryPath + "'");
        }
        kwxTrace("entry-resolved:" + resolved);
        kwxLoadModule(resolved);
        kwxTrace("run-main-return");
        return 0;
    };
})();
