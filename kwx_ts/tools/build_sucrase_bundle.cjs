// kwx_ts — one-time maintainer script that produces the committed Sucrase bundle.
//
// Usage (from kwx_ts/tools):
//   npm install
//   node build_sucrase_bundle.cjs
//
// The output, kwx_ts/third_party/sucrase.bundle.js, is a self-contained IIFE
// that publishes `globalThis.__kwx_sucrase`. It is committed and embedded into
// the kwx_ts executable; users of kwx_ts never run Node.
//
// The bundle is built for the browser platform (there is no filesystem, no
// Node built-ins, and no `require` inside the QuickJS host), targets ES2020,
// and forces `process.env.NODE_ENV` to "production" so Sucrase takes its
// production code path.

"use strict";

const path = require("path");
const esbuild = require("esbuild");

// require.resolve("sucrase") resolves to the CommonJS entry; its `exports`
// namespace (including `transform`) becomes the IIFE global.
const sucraseEntry = require.resolve("sucrase");

const outputFile = path.resolve(__dirname, "..", "third_party", "sucrase.bundle.js");

esbuild
    .build({
        entryPoints: [sucraseEntry],
        outfile: outputFile,
        bundle: true,
        format: "iife",
        globalName: "__kwx_sucrase",
        platform: "browser",
        target: "es2020",
        define: {
            "process.env.NODE_ENV": "\"production\""
        },
        logLevel: "info"
    })
    .then(() => {
        process.stdout.write("wrote " + outputFile + "\n");
    })
    .catch((error) => {
        process.stderr.write("build_sucrase_bundle failed: " + error + "\n");
        process.exit(1);
    });
