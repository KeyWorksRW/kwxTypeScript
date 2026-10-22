// kwx_ts — one-time maintainer script that produces the committed runtime
// assets.
//
// Usage (from kwx_ts/tools):
//   npm install
//   node build_runtime_bundle.cjs
//
// Two outputs are committed and embedded into the kwx_ts executable:
//
//   kwx_ts/runtime/kwx_runtime.js   — CommonJS bundle of the generated kwxFFI
//                                     TypeScript corpus (wx/mod.ts re-exports
//                                     every generated module plus
//                                     src/kwxApp.ts).
//   kwx_ts/runtime/kwx_platform.js  — the kwx/platform module, built from
//                                     src/kwx_platform.ts.
//
// They are separate assets on purpose: the corpus bundle calls dlopen() at
// module-eval time, so it must not resolve "kwx/platform" to a bundle that is
// still executing. esbuild keeps that specifier external and the host loader
// maps it to the embedded platform asset.
//
// Users of kwx_ts never run Node. Regenerate these files whenever wx/ or
// src/kwx_platform.ts changes, because they are snapshots of those sources.

"use strict";

const path = require("path");
const esbuild = require("esbuild");

const corpusEntry = path.resolve(__dirname, "..", "..", "wx", "mod.ts");
const corpusOutput = path.resolve(__dirname, "..", "runtime", "kwx_runtime.js");

const platformEntry = path.resolve(__dirname, "..", "..", "src", "kwx_platform.ts");
const platformOutput = path.resolve(__dirname, "..", "runtime", "kwx_platform.js");

async function build() {
    await esbuild.build({
        entryPoints: [corpusEntry],
        outfile: corpusOutput,
        bundle: true,
        format: "cjs",
        platform: "neutral",
        target: "es2020",
        // Resolved by the kwx_ts module loader to the embedded platform asset.
        external: ["kwx/platform"],
        logLevel: "info"
    });

    await esbuild.build({
        entryPoints: [platformEntry],
        outfile: platformOutput,
        bundle: true,
        format: "cjs",
        platform: "neutral",
        target: "es2020",
        logLevel: "info"
    });

    process.stdout.write("wrote " + corpusOutput + "\n");
    process.stdout.write("wrote " + platformOutput + "\n");
}

build().catch((error) => {
    process.stderr.write("build_runtime_bundle failed: " + error + "\n");
    process.exit(1);
});
