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

    // ------------------------------------------------------ source locations
    // The transpiled code carries an inline base64 v3 source map. It is stored
    // per module so `Error.prepareStackTrace` can translate a generated frame
    // back to its original `.ts` position. Decoding is lazy: a program that
    // never builds a stack trace never pays for it.
    var KWX_SOURCE_MAP_PREFIX = "sourceMappingURL=data:";
    var kwxModuleSources = Object.create(null);

    // Record the map text for `filePath`; the map itself is decoded on the
    // first error. A module without a marker (the `.js` fast path) keeps a null
    // mapText so it is never decoded at all.
    function kwxRegisterModuleSource(filePath, code) {
        var markerIndex = code.lastIndexOf(KWX_SOURCE_MAP_PREFIX);
        var mapText = null;
        if (markerIndex >= 0) {
            var payloadIndex =
                code.indexOf(",", markerIndex + KWX_SOURCE_MAP_PREFIX.length);
            if (payloadIndex >= 0) {
                var newlineIndex = code.indexOf("\n", payloadIndex);
                mapText = (newlineIndex < 0)
                    ? code.slice(payloadIndex + 1)
                    : code.slice(payloadIndex + 1, newlineIndex);
                mapText = mapText.trim();
            }
        }
        kwxModuleSources[filePath] = { mapText: mapText, map: undefined };
    }

    function kwxBase64Value(code) {
        if (code >= 65 && code <= 90) {
            return code - 65;
        }
        if (code >= 97 && code <= 122) {
            return code - 71;
        }
        if (code >= 48 && code <= 57) {
            return code + 4;
        }
        if (code === 43) {
            return 62;
        }
        if (code === 47) {
            return 63;
        }
        return -1;
    }

    // Standard RFC 4648 alphabet; '=' padding and whitespace are ignored, so a
    // payload without padding still decodes.
    function kwxBase64Decode(text) {
        var bytes = [];
        var buffer = 0;
        var bits = 0;
        for (var index = 0; index < text.length; ++index) {
            var value = kwxBase64Value(text.charCodeAt(index));
            if (value < 0) {
                continue;
            }
            buffer = ((buffer << 6) | value) & 0xFFFFFF;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                bytes.push((buffer >>> bits) & 0xFF);
            }
        }
        return bytes;
    }

    // Module paths may be non-ASCII, so decode UTF-8 instead of treating one
    // byte as one character.
    function kwxUtf8Decode(bytes) {
        var text = "";
        var index = 0;
        while (index < bytes.length) {
            var first = bytes[index++];
            var codePoint;
            if (first < 0x80) {
                codePoint = first;
            } else if (first < 0xE0) {
                codePoint = ((first & 0x1F) << 6) | (bytes[index++] & 0x3F);
            } else if (first < 0xF0) {
                codePoint = ((first & 0x0F) << 12) |
                    ((bytes[index++] & 0x3F) << 6) | (bytes[index++] & 0x3F);
            } else {
                codePoint = ((first & 0x07) << 18) |
                    ((bytes[index++] & 0x3F) << 12) |
                    ((bytes[index++] & 0x3F) << 6) | (bytes[index++] & 0x3F);
            }
            if (codePoint > 0xFFFF) {
                codePoint -= 0x10000;
                text += String.fromCharCode(0xD800 + (codePoint >> 10));
                text += String.fromCharCode(0xDC00 + (codePoint & 0x3FF));
            } else {
                text += String.fromCharCode(codePoint);
            }
        }
        return text;
    }

    // Base64 VLQ: low five bits per digit, bit 5 continues, bit 0 is the sign.
    function kwxDecodeVlq(text, index) {
        var value = 0;
        var shift = 0;
        var digit;
        do {
            digit = kwxBase64Value(text.charCodeAt(index++));
            if (digit < 0) {
                return [0, index];
            }
            value += (digit & 0x1F) << shift;
            shift += 5;
        } while (digit & 0x20);
        return [(value & 1) ? -(value >>> 1) : (value >>> 1), index];
    }

    // One entry per generated line; each entry lists its segments as
    // [generatedColumn, sourceIndex, originalLine, originalColumn] with 0-based
    // fields, after the deltas are accumulated.
    function kwxDecodeMappings(mappings) {
        var lines = [];
        var sourceIndex = 0;
        var originalLine = 0;
        var originalColumn = 0;
        var lineTexts = mappings.split(";");
        for (var lineIndex = 0; lineIndex < lineTexts.length; ++lineIndex) {
            var segments = [];
            var generatedColumn = 0;
            var segmentTexts = lineTexts[lineIndex].split(",");
            for (var segmentIndex = 0;
                segmentIndex < segmentTexts.length; ++segmentIndex) {
                var segmentText = segmentTexts[segmentIndex];
                if (segmentText.length === 0) {
                    continue;
                }
                var fields = [];
                var cursor = 0;
                while (cursor < segmentText.length) {
                    var decoded = kwxDecodeVlq(segmentText, cursor);
                    fields.push(decoded[0]);
                    if (decoded[1] <= cursor) {
                        break;
                    }
                    cursor = decoded[1];
                }
                if (fields.length === 0) {
                    continue;
                }
                generatedColumn += fields[0];
                if (fields.length >= 4) {
                    sourceIndex += fields[1];
                    originalLine += fields[2];
                    originalColumn += fields[3];
                    segments.push(
                        [generatedColumn, sourceIndex, originalLine, originalColumn]
                    );
                } else {
                    segments.push([generatedColumn, -1, -1, -1]);
                }
            }
            lines.push(segments);
        }
        return lines;
    }

    function kwxParseSourceMap(text) {
        var parsed = JSON.parse(kwxUtf8Decode(kwxBase64Decode(text)));
        if (!parsed || parsed.version !== 3 || typeof parsed.mappings !== "string") {
            throw new Error("kwx_ts: unsupported source map");
        }
        return {
            sources: parsed.sources || [],
            lines: kwxDecodeMappings(parsed.mappings)
        };
    }

    // `undefined` means "not decoded yet"; null means "no usable map", which is
    // remembered so a broken map is decoded once and not on every error.
    function kwxModuleSourceMap(filePath) {
        var entry = kwxModuleSources[filePath];
        if (!entry) {
            return null;
        }
        if (entry.map === undefined) {
            entry.map = null;
            if (entry.mapText) {
                try {
                    entry.map = kwxParseSourceMap(entry.mapText);
                } catch (error) {
                    entry.map = null;
                }
            }
        }
        return entry.map;
    }

    function kwxLookupOriginal(map, generatedLine, generatedColumn) {
        if (!map || generatedLine < 0 || generatedLine >= map.lines.length) {
            return null;
        }
        var segments = map.lines[generatedLine];
        var found = null;
        for (var index = 0; index < segments.length; ++index) {
            var segment = segments[index];
            if (segment[1] < 0) {
                continue;
            }
            if (segment[0] > generatedColumn) {
                break;
            }
            found = segment;
        }
        if (found) {
            return found;
        }
        for (var firstIndex = 0; firstIndex < segments.length; ++firstIndex) {
            if (segments[firstIndex][1] >= 0) {
                return segments[firstIndex];
            }
        }
        return null;
    }

    // The prologue occupies eval line 1, so the generated line is `line - 1` and
    // the generated columns are unshifted — exactly what the map describes.
    function kwxBuildFactory(filePath, code) {
        kwxRegisterModuleSource(filePath, code);
        if (typeof native.__kwx_eval_factory !== "function") {
            throw new Error("kwx_ts: __kwx_eval_factory is unavailable");
        }
        return native.__kwx_eval_factory(
            "(function (exports, module, require, __filename, __dirname) {\n" +
                code + "\n})\n",
            filePath
        );
    }

    // Translate one call site into a `    at fn (location)` line, or null to drop
    // the frame.
    function kwxFormatFrame(frame) {
        var fileName = frame.getFileName();
        var functionName = frame.getFunctionName();
        var hasFunction = (functionName !== null && functionName !== undefined &&
            functionName !== "");
        if (fileName === null || fileName === undefined) {
            return hasFunction ? "    at " + functionName + " (native)" : "    at <native>";
        }
        if (fileName === "<kwx_host_runtime>") {
            return null;
        }
        var line = frame.getLineNumber();
        var column = frame.getColumnNumber();
        var hasLine = (line !== null && line !== undefined);
        var location;
        if (kwxModuleSources[fileName] && hasLine && line > 1) {
            var generatedLine = line - 1;
            var map = kwxModuleSourceMap(fileName);
            var original = kwxLookupOriginal(
                map, generatedLine - 1, (column || 1) - 1
            );
            if (original) {
                location = (map.sources[original[1]] || fileName) + ":" +
                    (original[2] + 1) + ":" + (original[3] + 1);
            } else {
                location = fileName + ":" + generatedLine + ":" + column;
            }
        } else {
            location = fileName;
            if (hasLine) {
                location += ":" + line + ":" + column;
            }
        }
        return hasFunction
            ? "    at " + functionName + " (" + location + ")"
            : "    at " + location;
    }

    Error.prepareStackTrace = function (error, frames) {
        try {
            var lines = [];
            for (var index = 0; index < frames.length; ++index) {
                var formatted = kwxFormatFrame(frames[index]);
                if (formatted !== null) {
                    lines.push(formatted);
                }
            }
            return lines.join("\n");
        } catch (ignoredError) {
            // QuickJS sets `stack` to null when this callback throws, so a
            // decoding failure must never escape: report coordinates that do
            // not need a source map instead.
            var fallbackLines = [];
            try {
                for (var frameIndex = 0; frameIndex < frames.length; ++frameIndex) {
                    var fallbackFrame = frames[frameIndex];
                    var fallbackName = fallbackFrame.getFileName();
                    if (fallbackName === null || fallbackName === undefined ||
                        fallbackName === "<kwx_host_runtime>") {
                        continue;
                    }
                    var fallbackLine = fallbackFrame.getLineNumber();
                    if (fallbackLine === null || fallbackLine === undefined) {
                        fallbackLines.push("    at " + fallbackName);
                    } else {
                        fallbackLines.push(
                            "    at " + fallbackName + ":" + fallbackLine
                        );
                    }
                }
            } catch (ignoredFallbackError) {
                return "";
            }
            return fallbackLines.join("\n");
        }
    };

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
        var factory = kwxBuildFactory(filePath, code);
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
