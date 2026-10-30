# Third-party components

`kwx_ts` links the following third-party code into its executable. Users of
`kwx_ts` never download or install any of it; everything is produced by the
build.

| Component | Version | License | Source |
|-----------|---------|---------|--------|
| QuickJS-ng | `v0.17.0` | MIT | https://github.com/quickjs-ng/quickjs |
| SWC (`swc_core`) | `82.1.1` | Apache-2.0 | https://github.com/swc-project/swc |
| kwxFFI | `origin/main` | Apache-2.0 | https://github.com/KeyWorksRW/kwxFFI |

## How each is used

- **QuickJS-ng** — the JavaScript engine. Fetched by the repository root
  `CMakeLists.txt` via
  `FetchContent` (`GIT_TAG v0.17.0`) and linked statically as the `qjs` target.
- **SWC** — the TypeScript transpiler, used through the `swc_core` facade. The
  wrapper crate lives in `kwxts/third_party/swc_transformer` (see its README);
  it is built as a Rust `staticlib` and linked into the executable, so the
  transform runs as native code with no JavaScript transpiler to embed, load or
  evaluate. `Cargo.lock` pins the exact crate graph and Cargo fetches the
  sources from crates.io on the first build. The same library also backs
  `kwxts/tools/gen_bundle.cpp`, the build-time bundler, so the embedded
  bindings are transpiled with identical semantics.
- **kwxFFI** — the C ABI bindings library over wxWidgets. Fetched via
  `FetchContent` (`GIT_TAG origin/main`, `GIT_SHALLOW TRUE`) and linked
  statically together with a static wxWidgets.

## Generated artifacts

`kwx_runtime.js` and `kwx_platform.js` are build outputs, not committed
artifacts: CMake runs `gen_bundle.cpp` over `wx/` and `src/` and compiles the
result to QuickJS bytecode. No external JavaScript bundler is involved, so a
fresh checkout only needs a C++23 compiler and a Rust toolchain.
The TypeScript transformer has no committed binary artifact: it is compiled from
`swc_transformer/` plus the crates Cargo fetches, and the crate's version string
is reported by `kwx_swc_version()` and mixed into the transpile-cache key.
