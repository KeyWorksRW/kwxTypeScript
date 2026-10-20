// kwxTypeScript Demo — cross-platform wxWidgets GUI driven from TypeScript
//
// kwx_ts has no special entry convention: it loads the file you pass as a
// CommonJS-style module, so the `main()` call at the bottom of this file is
// what starts the application. Run it from the repository root:
//   bin\Release\kwx_ts.exe examples/demo.ts
//
// Everything imported below comes from the virtual module "kwx", which is
// embedded in the kwx_ts executable. No dist/ bundle and no kwxFFI DLL are
// read or loaded from disk at runtime.
//
// After regenerating wx/, rebuild the embedded corpus with:
//   cd kwx_ts/tools && node build_runtime_bundle.cjs

import {
    ALIGN_CENTER_VERTICAL,
    ALL,
    BOTH,
    createWxBoxSizer,
    createWxButton,
    createWxClosure,
    createWxFrame,
    createWxMenu,
    createWxMenuBar,
    createWxMessageDialog,
    createWxPanel,
    createWxStaticText,
    createWxString,
    createWxTextCtrl,
    DEFAULT_FRAME_STYLE,
    EVT_BUTTON,
    EVT_CLOSE_WINDOW,
    EVT_MENU,
    exitMainLoop,
    EXPAND,
    HORIZONTAL,
    ICON_INFORMATION,
    ID_ABOUT,
    ID_ANY,
    ID_EXIT,
    initialize,
    lib,
    OK,
    readAndDeleteWxString,
    run,
    setAppName,
    setTopWindow,
    shutdown,
    VERTICAL,
} from "kwx";
import { Callback, Pointer } from "kwx/platform";

export function main(): number
{
    // 1. Initialize wxWidgets
    if (!initialize())
    {
        console.error("Failed to initialize wxWidgets");
        return 1;
    }

    setAppName("kwxTypeScript Demo");

    // Pre-define a button ID so we can bind to it
    const BTN_CLICK_ME = 100;

    // 2. Create the main frame
    const titleStr = createWxString("kwxTypeScript Demo");

    const frame = createWxFrame(
        null, // parent
        ID_ANY, // id
        titleStr.ptr, // title (wxString*)
        -1,
        -1, // position (default)
        480,
        320, // size
        DEFAULT_FRAME_STYLE, // style
    )!;
    titleStr.Delete();

    // 3. Build menu bar
    const fileMenu = createWxMenu(createWxString("&File").ptr, 0)!;
    fileMenu.ptr; // keep reference

    const menuNew = createWxString("&New\tCtrl+N");
    const menuOpen = createWxString("&Open\tCtrl+O");
    const menuSave = createWxString("&Save\tCtrl+S");
    const menuExit = createWxString("E&xit\tAlt+F4");
    const menuUndo = createWxString("&Undo\tCtrl+Z");
    const menuRedo = createWxString("&Redo\tCtrl+Y");
    const menuCut = createWxString("Cu&t\tCtrl+X");
    const menuCopy = createWxString("&Copy\tCtrl+C");
    const menuPaste = createWxString("&Paste\tCtrl+V");
    const menuAbout = createWxString("&About");

    // File menu
    fileMenu.Append(ID_ANY, menuNew.ptr, createWxString("Create new").ptr, false);
    fileMenu.Append(ID_ANY, menuOpen.ptr, createWxString("Open file").ptr, false);
    fileMenu.Append(ID_ANY, menuSave.ptr, createWxString("Save file").ptr, false);
    fileMenu.AppendSeparator();
    fileMenu.Append(ID_EXIT, menuExit.ptr, createWxString("Exit application").ptr, false);

    // Edit menu
    const editMenu = createWxMenu(createWxString("&Edit").ptr, 0)!;
    editMenu.Append(ID_ANY, menuUndo.ptr, createWxString("").ptr, false);
    editMenu.Append(ID_ANY, menuRedo.ptr, createWxString("").ptr, false);
    editMenu.AppendSeparator();
    editMenu.Append(ID_ANY, menuCut.ptr, createWxString("").ptr, false);
    editMenu.Append(ID_ANY, menuCopy.ptr, createWxString("").ptr, false);
    editMenu.Append(ID_ANY, menuPaste.ptr, createWxString("").ptr, false);

    // Help menu
    const helpMenu = createWxMenu(createWxString("&Help").ptr, 0)!;
    helpMenu.Append(ID_ABOUT, menuAbout.ptr, createWxString("About this demo").ptr, false);

    // Menu bar
    const menuBar = createWxMenuBar(0)!;
    menuBar.Append(fileMenu.ptr, createWxString("&File").ptr);
    menuBar.Append(editMenu.ptr, createWxString("&Edit").ptr);
    menuBar.Append(helpMenu.ptr, createWxString("&Help").ptr);
    frame.SetMenuBar(menuBar.ptr);

    // Clean up menu strings
    menuNew.Delete();
    menuOpen.Delete();
    menuSave.Delete();
    menuExit.Delete();
    menuUndo.Delete();
    menuRedo.Delete();
    menuCut.Delete();
    menuCopy.Delete();
    menuPaste.Delete();
    menuAbout.Delete();

    // 4. Status bar
    frame.CreateStatusBar(1, 0, 0);
    const readyStr = createWxString("Ready");
    frame.SetStatusText(readyStr.ptr, 0);
    readyStr.Delete();

    // 5. Client area — panel + sizers
    const panel = createWxPanel(frame.ptr, ID_ANY, 0, 0, 0, 0, 0)!;

    const topSizer = createWxBoxSizer(VERTICAL)!;

    // --- Horizontal sizer: label + text control ---
    const hSizer = createWxBoxSizer(HORIZONTAL)!;

    const labelStr = createWxString("Text:");
    const hintStr = createWxString("hint text");

    const label = createWxStaticText(panel.ptr, ID_ANY, labelStr.ptr, -1, -1, -1, -1, 0)!;
    labelStr.Delete();

    const textCtrl = createWxTextCtrl(
        panel.ptr,
        ID_ANY,
        createWxString("").ptr,
        -1,
        -1,
        -1,
        -1,
        0,
    )!;
    // SetHint is on wxTextEntry, which wxTextCtrl inherits from in C++
    lib.symbols.wxTextEntry_SetHint(textCtrl.ptr, hintStr.ptr);
    hintStr.Delete();

    hSizer.AddWindow(label.ptr, 0, ALL | ALIGN_CENTER_VERTICAL, 5, null);
    hSizer.AddWindow(textCtrl.ptr, 1, ALL | EXPAND, 5, null);

    // --- "Click Me" button (use known ID for event binding) ---
    const btnStr = createWxString("Click Me");
    const button = createWxButton(panel.ptr, BTN_CLICK_ME, btnStr.ptr, -1, -1, -1, -1, 0)!;
    btnStr.Delete();

    // Assemble sizers
    topSizer.AddSizer(hSizer.ptr, 0, EXPAND | ALL, 5, null);
    topSizer.AddWindow(button.ptr, 0, ALL, 10, null);

    // Attach sizer to panel (inherited from wxWindow)
    panel.SetSizer(topSizer.ptr, false);

    // 6. Event handlers
    // IMPORTANT: Keep references to UnsafeCallback instances alive — if GC'd,
    // the native function pointer becomes dangling and causes a crash.

    // Button click
    const btnCb = new Callback({
        parameters: ["pointer", "pointer", "pointer"],
        result: "void",
    }, (_closureFun: Pointer, _data: Pointer, _event: Pointer) =>
    {
        const wxStrPtr = lib.symbols.wxTextCtrl_GetValue(textCtrl.ptr);
        const text = readAndDeleteWxString(wxStrPtr);

        if (text.length === 0)
        {
            const msgStr = createWxString("(no text entered)");
            const titleStr2 = createWxString("Demo");
            const dlg = createWxMessageDialog(
                frame.ptr,
                msgStr.ptr,
                titleStr2.ptr,
                OK | ICON_INFORMATION,
            )!;
            dlg.ShowModal();
            dlg.Delete();
            msgStr.Delete();
            titleStr2.Delete();
        } else
        {
            const msgStr = createWxString(text);
            const titleStr2 = createWxString("You typed");
            const dlg = createWxMessageDialog(
                frame.ptr,
                msgStr.ptr,
                titleStr2.ptr,
                OK | ICON_INFORMATION,
            )!;
            dlg.ShowModal();
            dlg.Delete();
            msgStr.Delete();
            titleStr2.Delete();
        }
    });
    const buttonClosure = createWxClosure(btnCb.pointer, null)!;
    lib.symbols.wxEvtHandler_Connect(
        button.ptr,
        BTN_CLICK_ME,
        BTN_CLICK_ME,
        EVT_BUTTON,
        buttonClosure.ptr,
    );

    // File > Exit
    const exitCb = new Callback({
        parameters: ["pointer", "pointer", "pointer"],
        result: "void",
    }, () =>
    {
        frame.Close(true);
    });
    const exitClosure = createWxClosure(exitCb.pointer, null)!;
    lib.symbols.wxEvtHandler_Connect(
        frame.ptr,
        ID_EXIT,
        ID_EXIT,
        EVT_MENU,
        exitClosure.ptr,
    );

    // Help > About
    const aboutCb = new Callback({
        parameters: ["pointer", "pointer", "pointer"],
        result: "void",
    }, () =>
    {
        const msgStr = createWxString(
            "kwxTypeScript Demo\nTypeScript bindings for wxWidgets via kwxFFI",
        );
        const titleStr2 = createWxString("About Demo");
        const dlg = createWxMessageDialog(
            frame.ptr,
            msgStr.ptr,
            titleStr2.ptr,
            OK | ICON_INFORMATION,
        )!;
        dlg.ShowModal();
        dlg.Delete();
        msgStr.Delete();
        titleStr2.Delete();
    });
    const aboutClosure = createWxClosure(aboutCb.pointer, null)!;
    lib.symbols.wxEvtHandler_Connect(
        frame.ptr,
        ID_ABOUT,
        ID_ABOUT,
        EVT_MENU,
        aboutClosure.ptr,
    );

    // Window close — exit main loop (do NOT call Close inside close handler)
    const closeCb = new Callback({
        parameters: ["pointer", "pointer", "pointer"],
        result: "void",
    }, () =>
    {
        exitMainLoop();
    });
    const closeClosure = createWxClosure(closeCb.pointer, null)!;
    lib.symbols.wxEvtHandler_Connect(
        frame.ptr,
        ID_ANY,
        ID_ANY,
        EVT_CLOSE_WINDOW,
        closeClosure.ptr,
    );

    // 7. Center and show
    frame.Center(BOTH);
    frame.Show();
    setTopWindow(frame.ptr);

    // 8. Run the event loop
    const exitCode = run();

    // 9. Cleanup — close callbacks before shutdown to avoid dangling pointers
    btnCb.close();
    exitCb.close();
    aboutCb.close();
    closeCb.close();
    shutdown();
    return exitCode;
}

main();
