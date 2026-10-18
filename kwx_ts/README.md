# kwx_ts

`kwx_ts` is a standalone, self-contained TypeScript runner for the kwxFFI
wxWidgets bindings. It embeds QuickJS-ng (JavaScript engine), Sucrase
(TypeScript transpiler), and a statically linked kwxFFI/wxWidgets, so a single
executable can run a `.ts` entry file that drives a native wxWidgets GUI.

There is no dependency on a system Deno, Node, or npm install at runtime. The
only dynamic libraries the executable loads are those provided by the operating
system.

## Usage

```
kwx_ts <entry.ts> [args...]
```

- `entry.ts` — the entry module. Relative and absolute filesystem paths are
  supported (see [Module resolution](#module-resolution)).
- `args...` — passed through to the program as `Deno.args`.

Example (Windows, Debug build, run from the repository root):

```
bin\Debug\kwx_ts.exe kwx_ts\examples\hello.ts
```

Example (macOS / Linux, Debug build):

```
bin/Debug/kwx_ts kwx_ts/examples/hello.ts
```

### Output streams and exit codes

- Program output (`console.log`, `console.info`, `console.debug`) goes to
  **stdout**.
- Diagnostics (`console.warn`, `console.error`, and loader/transpiler errors)
  go to **stderr**.
- Exit code `0` on success. A non-zero exit code is produced by `Deno.exit(n)`,
  by an uncaught error, or by a usage error (missing entry file → `2`).

### Startup tracing

Set `KWX_TS_TRACE` to any non-empty value to have the host print one timestamped
line per initialization phase to **stderr**. With the variable unset nothing is
printed and stdout is unaffected.

```
set KWX_TS_TRACE=1
bin\Debug\kwx_ts.exe kwx_ts\examples\hello.ts
```

```
[kwx-trace]      0.00 ms  main-begin
[kwx-trace]      0.51 ms  runtime-created
[kwx-trace]      4.23 ms  bindings-registered
[kwx-trace]      5.02 ms  deno-shim-evaluated
[kwx-trace]     12.71 ms  sucrase-bundle-evaluated
```

Native phases: `main-begin`, `runtime-created`, `context-created`,
`native-object-created`, `bindings-registered`, `deno-shim-evaluated`,
`sucrase-bundle-evaluated`, `run-main-begin`, `run-main-end`. Loader phases, per
module: `entry-resolved`, `module-read`, `transpile-begin` / `transpile-end`,
`module-compiled`, `module-factory-built` (the `new Function` compile) and
`module-executed`. JavaScript can add its own mark with
`__kwx_native.__kwx_trace(label)`.

The Sucrase bundle and the embedded `kwx` corpus are precompiled to QuickJS
bytecode at build time (see `CMakeLists.txt`), so an
`<embedded:…>` module skips `transpile-*`, `module-compiled` and
`module-factory-built`; `embedded-bytecode-loaded` marks the
`JS_ReadObject` + `JS_EvalFunction` step instead.

## Supported `Deno` surface

`kwx_ts` implements a deliberately small slice of the Deno API — exactly what
the kwxFFI binding corpus needs. Nothing else is available.

| API | Notes |
|-----|-------|
| `Deno.build.os` | Platform string (`"windows"`, `"darwin"`, `"linux"`). |
| `Deno.args` | Command-line arguments after the entry file. |
| `Deno.exit(code?)` | Terminates the process (sentinel-based unwind). |
| `Deno.dlopen(name, symbols)` | Resolves each requested symbol from the statically linked kwxFFI surface. It **never** opens a shared library — the `name` argument is informational only. |
| `Deno.UnsafePointer.of(value)` | Wraps a buffer/value as an opaque pointer. |
| `Deno.UnsafePointerView.getCString(ptr)` | Reads a NUL-terminated C string. |
| `Deno.UnsafeCallback(definition, fn)` | Registers a native callback; supports `.pointer`, `.close()`, `.ref()`, `.unref()`. |

Additionally available globally: `console` and `TextEncoder`.

Not available: `Deno.Command`, `Deno.readFile`, `Deno.writeFile`, `Deno.env`,
`Deno.cwd`, `fetch`, timers (`setTimeout` / `setInterval`), `queueMicrotask`,
and every other Deno/Web API.

## Module resolution

`kwx_ts` uses a CommonJS-style loader with a Sucrase transpile step:

- Specifiers are **relative or absolute filesystem paths**. There is no npm
  resolution and no remote URL support.
- Candidates are tried in order: the exact path, then `.ts`, `.tsx`, `.js`,
  `.mjs`, `.cjs` appended to the path, then `<path>/index.<ext>` with the same
  extension list.
- `.js` / `.cjs` files that contain no `import` / `export` tokens are loaded
  directly (fast path). Everything else — including every `.ts` file — is
  transpiled with the embedded Sucrase bundle using the `typescript` and
  `imports` transforms.
- The bare specifiers `"kwx"`, `"kwxTypeScript"`, and `"@kwx"` resolve to the
  **embedded binding bundle** rather than a file on disk.

### The `"kwx"` virtual module

Importing `"kwx"` gives you the entire kwxFFI binding corpus (all generated
`wx*` wrappers, constants, helpers, and the app functions) plus the raw `lib`
object:

```ts
import {
    BOTH,
    createWxString,
    DEFAULT_FRAME_STYLE,
    EVT_CLOSE_WINDOW,
    exit,
    ID_ANY,
    initialize,
    run,
    setTopWindow,
    shutdown,
    wxClosure,
    wxFrame,
} from "kwx";
```

This bundle is **embedded inside the executable** as a compiled CommonJS asset.
No `wx/` sources and no `dist/` bundle are read from disk at runtime.

See `examples/hello.ts` for a complete, minimal program.

## Event-loop model

wxWidgets owns the main event loop:

- `run()` calls into the native main loop and **blocks** until the loop ends.
- `exit()` requests the loop to stop (it must **not** be used to close a frame
  from inside a close handler — just call `exit()`).
- Pending QuickJS micro-task jobs are drained from wxWidgets dispatch hooks, so
  promise callbacks and jobs scheduled by JavaScript still run while the GUI is
  alive.
- JavaScript callbacks (registered via `Deno.UnsafeCallback`) execute
  **synchronously on the GUI thread**.

A typical program creates widgets, wires `wxClosure`-backed event handlers,
calls `frame.Show()` / `setTopWindow(frame.ptr)`, then calls `run()` and shuts
down with `shutdown()`.

> Keep every `Deno.UnsafeCallback` and `wxClosure` object referenced until after
> `run()` returns. If one is garbage-collected the native function pointer
> becomes dangling and the process can crash. Close them (`.close()`) after the
> loop exits.

## Limitations

- **No permissions model.** There is no `--allow-*` gating; the program simply
  has whatever access the bindings give it.
- **No network access** and **no npm / remote module resolution**.
- **Callbacks**: a fixed pool of **64** callback slots, each limited to the
  three-pointer `void` signature
  (`parameters: ["pointer", "pointer", "pointer"], result: "void"`).
- **No workers** and no multi-threading from JavaScript.
- **No ESM live bindings and no top-level `await`.** Modules are CommonJS;
  imports are snapshots.
- **Platforms**: Windows x64, macOS Universal, and Linux native. Windows ARM64
  is not supported.

## Building

The executable is built with CMake from the **repository root** (the single
root `CMakeLists.txt` builds kwx_ts, the binding generator, and the fetched
dependencies in one `build/` tree). QuickJS-ng and kwxFFI/wxWidgets are fetched
automatically via `FetchContent` on the first configure (network access
required once).

### Windows x64

Requires MSVC (or clang-cl) and Ninja.

```
cmake --preset ninja-multi
cmake --build build --config Debug --target kwx_ts
```

The executable is produced at `bin\Debug\kwx_ts.exe`.

### macOS Universal (`x86_64` + `arm64`)

Requires the Xcode command-line tools.

```
cmake --preset macos-universal
cmake --build build-macos --config Debug --target kwx_ts
lipo -info bin/Debug/kwx_ts
```

`lipo -info` should report the binary as containing both `x86_64` and `arm64`
slices (a Universal binary). The `.app` bundle fallback is **not** enabled, so
the output is a plain executable, not a `.app`.

### Linux native

Requires `pkg-config` and the development packages for `gtk+-3.0` and `x11`,
plus Ninja or Make.

```
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug --target kwx_ts
```

### Self-contained output

The produced executable is self-contained: it does not load kwxFFI, QuickJS, or
wxWidgets shared libraries. Apart from OS-provided libraries (Windows system
DLLs, macOS frameworks, Linux GTK3/X11 plus libc) nothing is loaded at runtime.

Windows ARM64 is unsupported.

## Maintainer note: regenerating the runtime bundle

`runtime/kwx_runtime.js` is a snapshot of the generated `wx/` corpus (it is the
embedded `"kwx"` virtual module). **Regenerate it whenever `wx/` is
regenerated** so the embedded bindings stay in sync:

```
cd tools
npm install
node build_runtime_bundle.cjs
```

The regenerated `runtime/kwx_runtime.js` is committed. Users of `kwx_ts` never
need Node or npm. The build compiles it to QuickJS bytecode with `qjsc`, so no
separate step is required after regeneration. See `third_party/README.md` for
the full set of one-time maintainer scripts.
