// kwx_ts — one-time maintainer script that produces the committed kwx runtime
// bundle.
//
// Usage (from kwx_ts/tools):
//   npm install
//   node build_runtime_bundle.cjs
//
// The output, kwx_ts/runtime/kwx_runtime.js, is a single CommonJS bundle of the
// generated kwxFFI TypeScript corpus (wx/mod.ts re-exports every generated
// module plus src/kwxApp.ts). It is committed and embedded into the kwx_ts
// executable as the virtual module "kwx"; users of kwx_ts never run Node.
//
// Regenerate this file whenever wx/ is regenerated (kwxgen), because it is a
// snapshot of that corpus.

"use strict";

const path = require("path");
const esbuild = require("esbuild");

const entry = path.resolve(__dirname, "..", "..", "wx", "mod.ts");
const outputFile = path.resolve(__dirname, "..", "runtime", "kwx_runtime.js");

esbuild
    .build({
        entryPoints: [entry],
        outfile: outputFile,
        bundle: true,
        format: "cjs",
        platform: "neutral",
        target: "es2020",
        logLevel: "info"
    })
    .then(() => {
        process.stdout.write("wrote " + outputFile + "\n");
    })
    .catch((error) => {
        process.stderr.write("build_runtime_bundle failed: " + error + "\n");
        process.exit(1);
    });
