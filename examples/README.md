# kwxTypeScript Examples

## Prerequisites

1. **Build the runtime** from the repository root:
   ```sh
   cmake --preset ninja-multi
   cmake --build build --config Release --target kwx_ts
   ```
   This produces `bin/Release/kwx_ts.exe` (Windows) or `bin/Release/kwx_ts`
   (macOS/Linux). QuickJS-ng, kwxFFI, and wxWidgets are fetched and linked
   statically; no shared library needs to be copied.

## Running the Demo

From the repository root:

```sh
bin/Release/kwx_ts examples/demo.ts
```

`demo.ts` is both the module and the entry point — it ends with a `main()` call,
which is all kwx_ts needs to start the application.

## Demo Description

A cross-platform native GUI window (480×320, centered) with:

- **Menu bar**: File (New, Open, Save, Exit), Edit (Undo, Redo, Cut, Copy, Paste), Help (About)
- **Status bar**: Displays "Ready"
- **Text input** with hint text "hint text"
- **"Click Me" button**: Shows the typed text in a message box (or "(no text entered)" if empty)
- **About dialog**: Identifies the demo
- **Window close**: Exits cleanly via `exitMainLoop()`

## Troubleshooting

### "Cannot find module"
Run the executable from the repository root so relative imports resolve, and
pass the entry file path (`examples/demo.ts`).

### Missing MSVC runtime (Windows)
Copy the required DLLs from your Visual Studio installation or the wxWidgets
build output directory.

### The window does not appear
Widgets are created synchronously, so an uncaught error is reported on stderr.
Set `KWX_TS_TRACE=1` to print startup phase timings.
