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
