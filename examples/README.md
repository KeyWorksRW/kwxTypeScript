# kwxTypeScript Examples

## Prerequisites

1. **Build kwxFFI shared library** from the project root:
   ```sh
   cmake --preset ninja-clang   # or your preferred preset
   cmake --build build
   ```
   This produces `lib/kwxFFI.dll` (Windows), `lib/libkwxFFI.so` (Linux), or `lib/libkwxFFI.dylib` (macOS).

2. **Copy the DLL to the project root** (or ensure it's on the system library path):
   ```sh
   # Windows
   copy lib\kwxFFI.dll .

   # Linux / macOS
   cp lib/libkwxFFI.so .   # or .dylib on macOS
   ```

3. **Install Deno**: https://deno.com

## Running the Demo

```sh
deno run --allow-ffi --allow-read --allow-env examples/demo.ts
```

### Permissions explained

| Flag | Reason |
|------|--------|
| `--allow-ffi` | Required for `Deno.dlopen()` to load kwxFFI |
| `--allow-read` | DLL loading may access the filesystem |
| `--allow-env` | DLL path resolution on some platforms |

## Demo Description

A cross-platform native GUI window (480×320, centered) with:

- **Menu bar**: File (New, Open, Save, Exit), Edit (Undo, Redo, Cut, Copy, Paste), Help (About)
- **Status bar**: Displays "Ready"
- **Text input** with hint text "hint text"
- **"Click Me" button**: Shows the typed text in a message box (or "(no text entered)" if empty)
- **About dialog**: Identifies the demo
- **Window close**: Exits cleanly via `ExitMainLoop()`

## Troubleshooting

### "Cannot find module" or DLL not found
Ensure `kwxFFI.dll` (or platform equivalent) is in the current working directory or on the system PATH.

### Missing MSVC runtime (Windows)
Copy the required DLLs from your Visual Studio installation or the wxWidgets build output directory.

### Permission denied
Add `--allow-ffi --allow-read --allow-env` flags to the `deno run` command.
