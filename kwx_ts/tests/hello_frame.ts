// kwx_ts — Phase 5 GUI test importing the virtual module "kwx".
//
// Proves the embedded kwx_runtime bundle loads (no dist/ or wx/ reads from
// disk) and drives a real wxWidgets frame: initialize, create + show a frame,
// set it as the top window, run the event loop, and shut down.
//
// Run from kwx_ts:
//   build/Debug/kwx_ts.exe tests/hello_frame.ts
//
// The close handler ends the loop; a one-shot timer closes the frame after a
// short delay so the test also completes unattended.

import {
    BOTH,
    createWxString,
    DEFAULT_FRAME_STYLE,
    EVT_CLOSE_WINDOW,
    exit,
    ID_ANY,
    initialize,
    lib,
    run,
    setTopWindow,
    shutdown,
    wxClosure,
    wxFrame
} from "kwx";

const native = (globalThis as { __kwx_native: Record<string, unknown> }).__kwx_native;

if (!initialize()) {
    throw new Error("kwx_ts: initialize() failed");
}

const titleStr = createWxString("kwx_ts hello frame");
const frame = wxFrame.Create(
    null,
    ID_ANY,
    titleStr.ptr,
    -1,
    -1,
    400,
    300,
    DEFAULT_FRAME_STYLE
)!;
titleStr.Delete();

if (!frame) {
    throw new Error("kwx_ts: wxFrame.Create returned null");
}

// Window close — end the main loop (never Close() inside a close handler).
const closeCallback = new Deno.UnsafeCallback(
    { parameters: ["pointer", "pointer", "pointer"], result: "void" },
    () => {
        exit();
    }
);
const closeClosure = wxClosure.Create(closeCallback.pointer, null)!;
lib.symbols.wxEvtHandler_Connect(
    frame.ptr,
    ID_ANY,
    ID_ANY,
    EVT_CLOSE_WINDOW,
    closeClosure.ptr
);

frame.Center(BOTH);
frame.Show();
setTopWindow(frame.ptr);

// Close the frame after a short delay so the test finishes without input.
(native.__kwx_test_after as (ms: number, fn: () => void) => void)(500, () => {
    frame.Close(true);
});

const exitCode = run();

closeCallback.close();
shutdown();

if (exitCode !== 0) {
    throw new Error("kwx_ts: main loop returned " + exitCode);
}

console.log("kwx_ts hello_frame ok");
