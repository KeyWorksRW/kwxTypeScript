// Following references are added so that a Deno linter will recognize Deno
// and esbuild types. The files don't need to exist -- they are only there
// if you are using a normal TypeScript linter and allow you to add the
// missing types.
/// <reference path="./_deno_types.d.ts" />
/// <reference path="./_esbuild_types.d.ts" />
// Create a single distribution bundle from both entry points
import * as esbuild from "esbuild";

// Single barrel entry that re-exports everything
const barrelPath = `${import.meta.dirname}/_bundle_entry.ts`;
const barrelContent = `
import * as _wx from "./wx/kwx_constants_gen.ts";
export const wx = _wx;
export * from "./wx/kwx_gen.ts";
export * from "./src/kwxApp.ts";
`;
await Deno.writeTextFile(barrelPath, barrelContent);

// Ensure dist/ directory exists
await Deno.mkdir("dist", { recursive: true });

await esbuild.build({
    entryPoints: [barrelPath],
    outfile: "dist/kwx.mjs",
    bundle: true,
    format: "esm",
    platform: "neutral",
});

// Clean up the temporary barrel entry
await Deno.remove(barrelPath);

esbuild.stop();
