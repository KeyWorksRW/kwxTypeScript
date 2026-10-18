// Phase 4 Deno shim smoke test (plain JS — no transpilation yet).
//
// Proves that the Deno compatibility shim can drive the whole wxWidgets
// lifecycle through the statically linked kwxFFI surface:
//   1. Deno.dlopen resolves every requested symbol from __kwx_native.
//   2. The CommonJS-style loader loads this file as a module (__kwx_run_main).
//   3. Deno.UnsafePointer.of + TextEncoder produce a usable C string buffer.
//   4. Deno.UnsafeCallback supplies a native closure pointer for an event.
//   5. A frame appears and the process exits with code 0.
//
// Deno.exit sets the exit code and unwinds through the host sentinel, so the
// scheduled timer ending the loop exercises that path too.

const native = __kwx_native;
const lib = Deno.dlopen("kwxFFI", {
    kwxApp_Initialize: { parameters: ["i32", "pointer"], result: "i32" },
    kwxApp_MainLoop: { parameters: [], result: "i32" },
    kwxApp_ExitMainLoop: { parameters: [], result: "void" },
    kwxApp_Shutdown: { parameters: [], result: "void" },
    kwxApp_SetTopWindow: { parameters: ["pointer"], result: "void" },
    wxString_CreateUTF8: { parameters: ["pointer"], result: "pointer" },
    wxString_Delete: { parameters: ["pointer"], result: "void" },
    wxFrame_Create: {
        parameters: ["pointer", "i32", "pointer", "i32", "i32", "i32", "i32", "i32"],
        result: "pointer",
    },
    wxWindow_Center: { parameters: ["pointer", "i32"], result: "void" },
    wxWindow_Show: { parameters: ["pointer"], result: "i32" },
    wxClosure_Create: { parameters: ["function", "pointer"], result: "pointer" },
    wxEvtHandler_Connect: {
        parameters: ["pointer", "i32", "i32", "i32", "pointer"],
        result: "i32",
    },
    expwxID_ANY: { parameters: [], result: "i32" },
    expwxDEFAULT_FRAME_STYLE: { parameters: [], result: "i32" },
    expwxBOTH: { parameters: [], result: "i32" },
    exp_wxEVT_CLOSE_WINDOW: { parameters: [], result: "i32" },
});

const $ = lib.symbols;

if (typeof native.__kwx_test_after !== "function") {
    throw new Error("kwx_ts: __kwx_test_after is not installed");
}

if (!$.kwxApp_Initialize(0, null)) {
    throw new Error("kwx_ts: kwxApp_Initialize failed");
}

// UTF-8 C string through the Deno surface (Deno.UnsafePointer.of).
const titleBuffer = new TextEncoder().encode("kwx_ts shim smoke\0");
const title = $.wxString_CreateUTF8(Deno.UnsafePointer.of(titleBuffer));
const frame = $.wxFrame_Create(
    null,
    $.expwxID_ANY(),
    title,
    -1,
    -1,
    420,
    320,
    $.expwxDEFAULT_FRAME_STYLE()
);
$.wxString_Delete(title);

if (!frame) {
    throw new Error("kwx_ts: wxFrame_Create returned null");
}

// Close-event handler: end the loop through the Deno surface.
const closeCallback = new Deno.UnsafeCallback(
    { parameters: ["pointer", "pointer", "pointer"], result: "void" },
    () => {
        $.kwxApp_ExitMainLoop();
    }
);
const closeClosure = $.wxClosure_Create(closeCallback.pointer, null);
$.wxEvtHandler_Connect(
    frame,
    $.expwxID_ANY(),
    $.expwxID_ANY(),
    $.exp_wxEVT_CLOSE_WINDOW(),
    closeClosure
);

$.wxWindow_Center(frame, $.expwxBOTH());
$.wxWindow_Show(frame);
$.kwxApp_SetTopWindow(frame);

// End the loop without user interaction, exercising Deno.exit's sentinel path.
native.__kwx_test_after(400, () => {
    Deno.exit(0);
});

const result = $.kwxApp_MainLoop();

$.kwxApp_Shutdown();
closeCallback.close();
lib.close();

if (result !== 0) {
    throw new Error("kwx_ts: main loop returned " + result);
}

console.log("kwx_ts shim smoke ok");
