// kwx_ts — host runtime.
//
// Publishes `globalThis.kwxHost` (the native binding bridge), installs
// `console` and `TextEncoder`, and provides the CommonJS-style module loader
// that transpiles TypeScript with the statically linked SWC transformer.
//
// Everything here runs on top of the object created in quickjs_host.cpp:
// `kwxHost.__kwx_resolve_symbol` returns a thunk for one statically linked
// kwxFFI symbol. The public FFI surface lives in kwx_platform.js, which
// generated bindings import through the bare specifier "kwx/platform".
//
// Bare specifiers resolved in-binary:
//   "kwx", "kwxTypeScript", "@kwx" -> the embedded binding corpus
//   "kwx/platform"                 -> the embedded platform module

(function () {
    "use strict";

    var rawNative = globalThis.kwxHost;
    if (!rawNative) {
        throw new Error("kwx_ts: kwxHost is not available");
    }

    // Generated FFI thunks are created on first use and cached here. Both
    // dlopen() and direct `kwxHost.<symbol>` access go through this table, so
    // the 5,803-symbol corpus costs nothing until a symbol is used.
    var kwxSymbolCache = Object.create(null);
    var kwxResolvedSymbolCount = 0;

    function kwxResolveSymbolFunction(key) {
        var cached = kwxSymbolCache[key];
        if (cached !== undefined) {
            return cached;
        }
        var fn = rawNative.__kwx_resolve_symbol(key);
        if (typeof fn !== "function") {
            return undefined;
        }
        kwxSymbolCache[key] = fn;
        ++kwxResolvedSymbolCount;
        return fn;
    }

    // `kwxHost` keeps its published shape (helpers are direct properties) but
    // resolves a corpus symbol from the binding table the first time it is
    // read. User scripts that call `kwxHost.SomeSymbol()` keep working.
    var native = new Proxy(rawNative, {
        get: function (target, key) {
            var direct = target[key];
            if (direct !== undefined) {
                return direct;
            }
            if (typeof key !== "string") {
                return undefined;
            }
            return kwxResolveSymbolFunction(key);
        },
        has: function (target, key) {
            if (key in target) {
                return true;
            }
            if (typeof key !== "string") {
                return false;
            }
            return kwxResolveSymbolFunction(key) !== undefined;
        }
    });

    globalThis.kwxHost = native;

    // ------------------------------------------------------------ trace marks
    // Opt-in startup timing: kwxTrace is a cheap no-op unless KWX_TS_TRACE is
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

    // ---------------------------------------------------------- transpilation
    // TypeScript is lowered to CommonJS by the SWC transformer linked into the
    // executable (native.__kwx_swc_transpile) — no embedded JavaScript
    // transpiler is loaded or evaluated. The host caches transpiles on disk, so
    // an unchanged module never reaches the transformer at all.
    var kwxSwcTransformSeen = false;

    function kwxTranspile(source, filePath) {
        if (!kwxSwcTransformSeen) {
            kwxSwcTransformSeen = true;
            kwxTrace("swc-transform-first:" + filePath);
        }
        kwxTrace("transpile-begin:" + filePath);
        var transformed = native.__kwx_swc_transpile(source, filePath);
        kwxTrace("transpile-end:" + filePath);
        return transformed;
    }

    // Fast path: a .js/.cjs module with no import/export tokens is already valid
    // CommonJS, so skip the transformer entirely. Everything else (including
    // every .ts file) is transpiled.
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
    // "kwx/platform" is a separate asset on purpose: the corpus bundle calls
    // dlopen() at module-eval time, so resolving it to the still-executing
    // corpus bundle would hand back a half-populated exports object.
    var KWX_VIRTUAL_MODULES = {
        "kwx": "kwx_runtime",
        "kwxTypeScript": "kwx_runtime",
        "@kwx": "kwx_runtime",
        "kwx/platform": "kwx_platform"
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
        var cached = native.__kwx_transpile_cache_get(source, filePath);
        if (cached !== null && cached !== undefined) {
            kwxTrace("transpile-cache-hit:" + filePath);
            return cached;
        }
        kwxTrace("transpile-cache-miss:" + filePath);
        var code = kwxTranspile(source, filePath);
        native.__kwx_transpile_cache_put(source, filePath, code);
        return code;
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
        kwxTrace("symbols-resolved:" + kwxResolvedSymbolCount);
        kwxTrace("run-main-return");
        return 0;
    };
})();
