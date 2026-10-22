# Third-party components

`kwx_ts` links the following third-party code into its executable. Users of
`kwx_ts` never download or install any of it; everything is produced by the
build.

| Component | Version | License | Source |
|-----------|---------|---------|--------|
| QuickJS-ng | `v0.17.0` | MIT | https://github.com/quickjs-ng/quickjs |
| SWC (`swc_core`) | `82.1.1` | Apache-2.0 | https://github.com/swc-project/swc |
| esbuild | `0.25.0` | MIT | https://github.com/evanw/esbuild (build-time only) |
| kwxFFI | `origin/main` | Apache-2.0 | https://github.com/KeyWorksRW/kwxFFI |

## How each is used

- **QuickJS-ng** — the JavaScript engine. Fetched by the repository root
  `CMakeLists.txt` via
  `FetchContent` (`GIT_TAG v0.17.0`) and linked statically as the `qjs` target.
- **SWC** — the TypeScript transpiler, used through the `swc_core` facade. The
  wrapper crate lives in `kwx_ts/third_party/swc_transformer` (see its README);
  it is built as a Rust `staticlib` and linked into the executable, so the
  transform runs as native code with no JavaScript transpiler to embed, load or
  evaluate. `Cargo.lock` pins the exact crate graph and Cargo fetches the
  sources from crates.io on the first build.
- **esbuild** — build-time tool used only by the maintainer script below to
  produce `kwx_runtime.js` and `kwx_platform.js`. It is never embedded and
  never shipped.
- **kwxFFI** — the C ABI bindings library over wxWidgets. Fetched via
  `FetchContent` (`GIT_TAG origin/main`, `GIT_SHALLOW TRUE`) and linked
  statically together with a static wxWidgets.

## Regenerating the committed artifacts

`runtime/kwx_runtime.js` and `runtime/kwx_platform.js` are one-time maintainer
artifacts: they snapshot the generated `wx/` corpus and `src/kwx_platform.ts`.
Regenerate them only when those inputs change, then commit the results:

```
cd kwx_ts/tools && npm install && node build_runtime_bundle.cjs
```

- `build_runtime_bundle.cjs` → `kwx_ts/runtime/kwx_runtime.js` (the embedded
  `"kwx"` virtual module; a snapshot of the generated `wx/` corpus) and
  `kwx_ts/runtime/kwx_platform.js` (the `kwx/platform` module).

`kwx_ts/tools/package.json` pins the exact `esbuild` version used to produce
these artifacts.

The TypeScript transformer has no committed binary artifact: it is compiled from
`swc_transformer/` plus the crates Cargo fetches, and the crate's version string
is reported by `kwx_swc_version()` and mixed into the transpile-cache key.
