// kwxApp TypeScript FFI bindings — hand-written application lifecycle layer.
//
// kwxApp is NOT part of the standard kwxFFI headers — it's built from
// kwxFFI's examples/CApp/ and linked into the same shared library.
// This file opens the library a second time specifically for kwxApp symbols.

const _libName: string = ({
  windows: "kwxFFI.dll",
  linux:   "libkwxFFI.so",
  darwin:  "libkwxFFI.dylib",
} as Record<string, string>)[Deno.build.os] ?? "kwxFFI";

const _appLib = Deno.dlopen(_libName, {
  // --- Application Lifecycle ---
  kwxApp_Initialize:       { parameters: ["i32", "pointer"], result: "i32" },
  kwxApp_MainLoop:         { parameters: [], result: "i32" },
  kwxApp_ExitMainLoop:     { parameters: [], result: "void" },
  kwxApp_Shutdown:         { parameters: [], result: "void" },
  kwxApp_IsTerminating:    { parameters: [], result: "i32" },

  // --- Application Properties ---
  kwxApp_GetAppName:       { parameters: [], result: "pointer" },
  kwxApp_SetAppName:       { parameters: ["pointer"], result: "void" },
  kwxApp_GetVendorName:    { parameters: [], result: "pointer" },
  kwxApp_SetVendorName:    { parameters: ["pointer"], result: "void" },
  kwxApp_GetTopWindow:     { parameters: [], result: "pointer" },
  kwxApp_SetTopWindow:     { parameters: ["pointer"], result: "void" },
  kwxApp_SetExitOnFrameDelete: { parameters: ["i32"], result: "void" },
  kwxApp_GetExitOnFrameDelete: { parameters: [], result: "i32" },

  // --- System Information ---
  kwxApp_GetDisplaySize:   { parameters: ["pointer", "pointer"], result: "void" },
  kwxApp_GetMousePosition: { parameters: ["pointer", "pointer"], result: "void" },
  kwxApp_GetOsVersion:     { parameters: ["pointer", "pointer"], result: "i32" },
  kwxApp_GetOsDescription: { parameters: [], result: "pointer" },
  kwxApp_GetUserId:        { parameters: [], result: "pointer" },
  kwxApp_GetUserName:      { parameters: [], result: "pointer" },

  // --- Event Processing ---
  kwxApp_Pending:          { parameters: [], result: "i32" },
  kwxApp_Dispatch:         { parameters: [], result: "void" },
  kwxApp_Yield:            { parameters: [], result: "i32" },
  kwxApp_SafeYield:        { parameters: ["pointer"], result: "i32" },

  // --- Idle Timer ---
  kwxApp_SetIdleCallback:  { parameters: ["i32", "pointer", "pointer"], result: "void" },
  kwxApp_GetIdleInterval:  { parameters: [], result: "i32" },

  // --- Utilities ---
  kwxApp_InitAllImageHandlers: { parameters: [], result: "void" },
  kwxApp_EnableTooltips:   { parameters: ["i32"], result: "void" },
  kwxApp_SetTooltipDelay:  { parameters: ["i32"], result: "void" },
  kwxApp_Bell:             { parameters: [], result: "void" },
  kwxApp_Sleep:            { parameters: ["i32"], result: "void" },
  kwxApp_MilliSleep:       { parameters: ["i32"], result: "void" },
  kwxApp_FindWindowById:   { parameters: ["i32", "pointer"], result: "pointer" },
  kwxApp_FindWindowByLabel:{ parameters: ["pointer", "pointer"], result: "pointer" },
  kwxApp_FindWindowByName: { parameters: ["pointer", "pointer"], result: "pointer" },
  kwxApp_FreeString:       { parameters: ["pointer"], result: "void" },
});

const $ = _appLib.symbols;

// --- Public API ---

/** Initialize wxWidgets. Call once before any other wx functions. */
export function initialize(argc = 0, argv: Deno.PointerValue = null): boolean {
  return $.kwxApp_Initialize(argc, argv) !== 0;
}

/** Run the main event loop. Blocks until the application exits. */
export function run(): number {
  return $.kwxApp_MainLoop();
}

/** Exit the main event loop. Call from an event handler to quit. */
export function exit(): void {
  $.kwxApp_ExitMainLoop();
}

/** Shutdown wxWidgets and clean up. Optional — called automatically on exit. */
export function shutdown(): void {
  $.kwxApp_Shutdown();
}

/** Returns true if the application is terminating. */
export function isTerminating(): boolean {
  return $.kwxApp_IsTerminating() !== 0;
}

// --- Application Properties ---

export function getAppName(): string {
  const ptr = $.kwxApp_GetAppName();
  return ptr ? Deno.UnsafePointerView.getCString(ptr) : "";
}

export function setAppName(name: string): void {
  const buf = new TextEncoder().encode(name + "\0");
  $.kwxApp_SetAppName(Deno.UnsafePointer.of(buf));
}

export function getVendorName(): string {
  const ptr = $.kwxApp_GetVendorName();
  return ptr ? Deno.UnsafePointerView.getCString(ptr) : "";
}

export function setVendorName(name: string): void {
  const buf = new TextEncoder().encode(name + "\0");
  $.kwxApp_SetVendorName(Deno.UnsafePointer.of(buf));
}

export function getTopWindow(): Deno.PointerValue {
  return $.kwxApp_GetTopWindow();
}

export function setTopWindow(window: Deno.PointerValue): void {
  $.kwxApp_SetTopWindow(window);
}

export function setExitOnFrameDelete(flag: boolean): void {
  $.kwxApp_SetExitOnFrameDelete(flag ? 1 : 0);
}

// --- System Information ---

export function getDisplaySize(): { width: number; height: number } {
  const wBuf = new Int32Array(1);
  const hBuf = new Int32Array(1);
  $.kwxApp_GetDisplaySize(Deno.UnsafePointer.of(wBuf), Deno.UnsafePointer.of(hBuf));
  return { width: wBuf[0], height: hBuf[0] };
}

// --- Utilities ---

export function initAllImageHandlers(): void {
  $.kwxApp_InitAllImageHandlers();
}

export function bell(): void {
  $.kwxApp_Bell();
}
