// Phase 2 event-loop/re-entrancy spike.
//
// Proves three things in one run:
//   1. JS can drive the wxWidgets application lifecycle (__kwx_native).
//   2. A wx event (one-shot timer) can call back into QuickJS.
//   3. JS can call back into wx from that callback and end the loop.
//
// Exit code 0 means the callback fired and the loop exited cleanly.

const native = __kwx_native;

if (typeof native.kwxApp_Initialize !== "function") {
    throw new Error("kwx_ts: spike bindings are not installed");
}

if (!native.kwxApp_Initialize(0, null)) {
    throw new Error("kwx_ts: kwxApp_Initialize failed");
}

const title = native.wxString_CreateUTF8("kwx_ts loop spike");
const frame = native.wxFrame_Create(
    null,
    native.expwxID_ANY(),
    title,
    -1,
    -1,
    400,
    300,
    native.expwxDEFAULT_FRAME_STYLE()
);
native.wxString_Delete(title);

if (!frame) {
    throw new Error("kwx_ts: wxFrame_Create returned null");
}

native.wxWindow_Center(frame, native.expwxBOTH());
native.wxWindow_Show(frame);
native.kwxApp_SetTopWindow(frame);

native.__kwx_test_after(300, () => {
    native.__kwx_print(0, "callback ok");
    native.kwxApp_ExitMainLoop();
});

const result = native.kwxApp_MainLoop();
native.kwxApp_Shutdown();

if (result !== 0) {
    native.__kwx_print(2, "kwx_ts: main loop returned " + result);
    throw new Error("kwx_ts: spike failed");
}
