# Third-party components

`kwx_ts` embeds the following third-party code in its executable. All of it is
committed to this repository; users of `kwx_ts` never download or install these
dependencies.

| Component | Version | License | Source |
|-----------|---------|---------|--------|
| QuickJS-ng | `v0.17.0` | MIT | https://github.com/quickjs-ng/quickjs |
| Sucrase | `3.35.1` | MIT | https://github.com/alangpierce/sucrase |
| esbuild | `0.25.0` | MIT | https://github.com/evanw/esbuild (build-time only) |
| kwxFFI | `origin/main` | Apache-2.0 | https://github.com/KeyWorksRW/kwxFFI |

## How each is used

- **QuickJS-ng** — the JavaScript engine. Fetched by the repository root
  `CMakeLists.txt` via
  `FetchContent` (`GIT_TAG v0.17.0`) and linked statically as the `qjs` target.
- **Sucrase** — the TypeScript transpiler. Bundled into
  `kwx_ts/third_party/sucrase.bundle.js` (a self-contained IIFE that publishes
  `globalThis.__kwx_sucrase`), embedded into the executable, and used to
  transpile every `.ts` module at load time.
- **esbuild** — build-time tool used only by the maintainer scripts below to
  produce `sucrase.bundle.js` and `kwx_runtime.js`. It is never embedded and
  never shipped.
- **kwxFFI** — the C ABI bindings library over wxWidgets. Fetched via
  `FetchContent` (`GIT_TAG origin/main`, `GIT_SHALLOW TRUE`) and linked
  statically together with a static wxWidgets.

## Regenerating the committed artifacts

Both `third_party/sucrase.bundle.js` and `runtime/kwx_runtime.js` are one-time
maintainer artifacts. Regenerate them only when their inputs change (a Sucrase
version bump, or a `wx/` regeneration), then commit the results:

```
cd kwx_ts/tools && npm install && node build_sucrase_bundle.cjs && node build_runtime_bundle.cjs
```

- `build_sucrase_bundle.cjs` → `kwx_ts/third_party/sucrase.bundle.js`.
- `build_runtime_bundle.cjs` → `kwx_ts/runtime/kwx_runtime.js` (the embedded
  `"kwx"` virtual module; a snapshot of the generated `wx/` corpus).

`kwx_ts/tools/package.json` pins the exact `sucrase` and `esbuild` versions used
to produce these artifacts.
