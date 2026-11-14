// Quick test entry point.
//
// Run from the repository root:
//   bin\Release\kwxts.exe tests\quick\quick_main.ts
// or type-check only:
//   build_quick.cmd
//
// The frame is created hidden by quick_mainframe.ts and explicitly shown here.
// Closing the window (title-bar close button) is the only way to exit.
//
// quick.wxui holds both forms, so both generated entries are imported statically.
// The embedded kwx_ts runtime transpiles modules but does not implement dynamic
// import(), so a missing dialog form fails the module load rather than being skipped.

import {
    EVT_CLOSE_WINDOW,
    ID_ANY,
    createWxClosure,
    initialize,
    initAllImageHandlers,
    run,
    setAppName,
    setTopWindow,
    shutdown,
} from "kwx";
import { createQuickMainFrame } from "./quick_mainframe.ts";
import { createQuickDialog } from "./quick_dlg.ts";
import { Callback, exit, Pointer } from "kwx/platform";

export function main(): number
{
    // 1. Initialize wxWidgets
    if (!initialize())
    {
        console.error("Failed to initialize wxWidgets");
        return 1;
    }

    setAppName("Quick");
    // The generated form decodes its embedded artwork, which needs the handlers.
    initAllImageHandlers();

    // 2. Build the (hidden) main frame
    const frame = createQuickMainFrame(null);

    // 3. Show it and register it as the top window
    frame.Show();
    setTopWindow(frame.ptr);

    // 4. Bind the frame close before running the modal dialog, mirroring the Go runner:
    //    wxWidgets vetoes a frame close while an app-modal dialog is open, and this
    //    project never clicks the dialog's OK button, so the close request has to end
    //    the modal dialog itself. Bind after the generated handler so this one runs
    //    first, then destroy the frame once the modal loop has unwound.
    const dialog = createQuickDialog(frame.ptr);
    let closeRequested = false;
    const frameCloseCallback = new Callback(
        { parameters: ["pointer", "pointer", "pointer"], result: "void" },
        (_closureFun: Pointer, _data: Pointer, _eventPtr: Pointer) =>
        {
            closeRequested = true;
            dialog.EndModal(0);
        },
    );
    const frameCloseClosure = createWxClosure(frameCloseCallback.pointer, null)!;
    frame.Bind(ID_ANY, ID_ANY, EVT_CLOSE_WINDOW, frameCloseClosure.ptr);

    // 5. Run the dialog modally
    dialog.ShowModal();

    if (closeRequested)
    {
        // The frame is destroyed before the event loop ever starts; returning here lets the
        // process exit with 0. Do not call shutdown(): the pending close event would re-enter
        // the handler after the JS context is gone.
        frame.Destroy();
        return 0;
    }

    // 6. Run the event loop. Closing the last top-level window (title-bar
    //    close button) ends the loop.
    const exitCode = run();

    // 7. Cleanup
    shutdown();
    return exitCode;
}

// Match the samples' bootstrap: exit() reports the code from run() as the process status.
exit(main());
