// kwx_ts example — minimal wxWidgets GUI driven from TypeScript.
//
// Run with the kwx_ts executable (build/Debug/kwx_ts.exe on Windows):
//   build/Debug/kwx_ts.exe examples/hello.ts
//
// Unlike examples/demo.ts this imports from the virtual module "kwx", which is
// embedded inside the kwx_ts executable — no dist/ bundle and no wx/ sources are
// read from disk at runtime.

import {
    ALL,
    BOTH,
    createWxString,
    DEFAULT_FRAME_STYLE,
    EVT_BUTTON,
    EVT_CLOSE_WINDOW,
    exit,
    ID_ANY,
    initialize,
    lib,
    run,
    setAppName,
    setTopWindow,
    shutdown,
    VERTICAL,
    wxBoxSizer,
    wxButton,
    wxClosure,
    wxFrame,
    wxPanel,
    wxStaticText
} from "kwx";

// Fixed command id so the click handler can be bound before the button exists.
const BUTTON_ID = 1000;

/** Builds the frame, wires its events, and runs the wxWidgets main loop. */
export function main(): number
{
    if (!initialize())
    {
        console.error("kwx_ts hello: initialize() failed");
        return 1;
    }

    setAppName("kwx_ts hello");

    const titleStr = createWxString("kwx_ts hello");
    const frame = wxFrame.Create(
        null,
        ID_ANY,
        titleStr.ptr,
        -1,
        -1,
        360,
        160,
        DEFAULT_FRAME_STYLE
    )!;
    titleStr.Delete();

    if (!frame)
    {
        console.error("kwx_ts hello: wxFrame.Create returned null");
        shutdown();
        return 1;
    }

    const panel = wxPanel.Create(frame.ptr, ID_ANY, 0, 0, 0, 0, 0)!;
    const topSizer = wxBoxSizer.Create(VERTICAL)!;

    const labelStr = createWxString("Press the button");
    const label = wxStaticText.Create(panel.ptr, ID_ANY, labelStr.ptr, -1, -1, -1, -1, 0)!;
    labelStr.Delete();

    const buttonStr = createWxString("Click Me");
    const button = wxButton.Create(panel.ptr, BUTTON_ID, buttonStr.ptr, -1, -1, -1, -1, 0)!;
    buttonStr.Delete();

    topSizer.AddWindow(label.ptr, 0, ALL, 10, null);
    topSizer.AddWindow(button.ptr, 0, ALL, 10, null);
    panel.SetSizer(topSizer.ptr);

    let clickCount = 0;

    // Keep every callback and closure referenced until after run() returns — a
    // collected UnsafeCallback leaves a dangling native function pointer.
    const buttonCallback = new Deno.UnsafeCallback(
        { parameters: ["pointer", "pointer", "pointer"], result: "void" },
        () =>
        {
            clickCount += 1;
            const message = createWxString(`Button clicked ${clickCount} time(s)`);
            label.SetLabel(message.ptr);
            message.Delete();
        }
    );
    const buttonClosure = wxClosure.Create(buttonCallback.pointer, null)!;
    lib.symbols.wxEvtHandler_Connect(
        button.ptr,
        BUTTON_ID,
        BUTTON_ID,
        EVT_BUTTON,
        buttonClosure.ptr
    );

    // Window close — end the main loop (never Close() inside a close handler).
    const closeCallback = new Deno.UnsafeCallback(
        { parameters: ["pointer", "pointer", "pointer"], result: "void" },
        () =>
        {
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

    // Printed immediately before the blocking event loop so a launched run
    // proves transpile + "kwx" load + initialize() + frame creation succeeded.
    console.log("kwx_ts hello example ready");

    const exitCode = run();

    buttonCallback.close();
    closeCallback.close();
    shutdown();
    return exitCode;
}

Deno.exit(main());
