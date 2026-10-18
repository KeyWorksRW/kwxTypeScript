// Quick test entry point — the actual main() invoked by Deno.exit(main()).
//
// Run with:
//   deno run --allow-ffi --allow-read --allow-env tests/quick/quick_main.ts
//
// The kwxFFI.dll (or libkwxFFI.so / libkwxFFI.dylib) must be in the library
// search path. Copy it from build/lib/ to the project root, or add the lib/
// directory to PATH / LD_LIBRARY_PATH / DYLD_LIBRARY_PATH.
//
// The frame is created hidden by quick_mainframe.ts and explicitly shown here.
// Closing the window (title-bar close button) is the only way to exit.

import {
    initialize,
    run,
    setAppName,
    setTopWindow,
    shutdown,
} from "../../dist/kwx.mjs";
import { createMainFrame } from "./quick_mainframe.ts";

export function main(): number
{
    // 1. Initialize wxWidgets
    if (!initialize())
    {
        console.error("Failed to initialize wxWidgets");
        return 1;
    }

    setAppName("Quick");

    // 2. Build the (hidden) main frame
    const frame = createMainFrame();

    // 3. Show it and register it as the top window
    frame.Show();
    setTopWindow(frame.ptr);

    // 4. Run the event loop. No event handlers are installed — closing the
    //    last top-level window (title-bar close button) ends the loop.
    const exitCode = run();

    // 5. Cleanup
    shutdown();
    return exitCode;
}

Deno.exit(main());
