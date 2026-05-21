/*
 * kwxApp - Pure C interface for wxWidgets application startup
 *
 * This is the C++ implementation that provides the C interface declared in
 * kwxApp.h. Foreign languages never see this file - they only use kwxApp.h.
 *
 * NOTE: This is a local copy of the upstream kwxFFI examples/CApp/ files,
 *       adapted for the kwxTypeScript project.
 */

#include "kwxApp.h"

#include <wx/cmdline.h>
#include <wx/tooltip.h>
#include <wx/wx.h>

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

/*-----------------------------------------------------------------------------
    Internal state
-----------------------------------------------------------------------------*/
namespace {
bool g_initialized = false;
bool g_terminating = false;

// Idle timer support
kwxApp_IdleCallback g_idleCallback = nullptr;
void *g_idleCallbackData = nullptr;

// Cached strings for Get functions (to avoid returning dangling pointers)
wxString g_cachedAppName;
wxString g_cachedVendorName;

#ifdef _WIN32
// Activation context for Common Controls v6 (visual styles) when loaded via
// script host
HANDLE g_actCtx = INVALID_HANDLE_VALUE;
ULONG_PTR g_actCtxCookie = 0;
#endif

// Helper to convert wxString to C string (caller must free)
char *wxStringToNewCStr(const wxString &str) {
  const char *utf8 = str.utf8_str();
  size_t len = strlen(utf8) + 1;
  char *result = static_cast<char *>(malloc(len));
  if (result) {
    memcpy(result, utf8, len);
  }
  return result;
}
} // namespace

/*-----------------------------------------------------------------------------
    Idle Timer Class
-----------------------------------------------------------------------------*/
class kwxIdleTimer : public wxTimer {
public:
  void Notify() override {
    if (g_idleCallback) {
      g_idleCallback(g_idleCallbackData);
    }
  }
};

static kwxIdleTimer *g_idleTimer = nullptr;

/*-----------------------------------------------------------------------------
    The wxApp subclass (hidden from C callers)
-----------------------------------------------------------------------------*/
class kwxAppImpl : public wxApp {
public:
  bool OnInit() override {
    if (!wxApp::OnInit()) {
      return false;
    }
    g_idleTimer = new kwxIdleTimer();
    return true;
  }

  int OnExit() override {
    g_terminating = true;
    if (g_idleTimer) {
      g_idleTimer->Stop();
      delete g_idleTimer;
      g_idleTimer = nullptr;
    }
    return wxApp::OnExit();
  }

  // Prevent wxApp from failing on foreign language command line args
  void OnInitCmdLine(wxCmdLineParser &parser) override {
    parser.SetCmdLine("");
  }

  bool OnCmdLineParsed(wxCmdLineParser &) override { return true; }
};

// This is the actual wxApp instance
wxIMPLEMENT_APP_NO_MAIN(kwxAppImpl);

/*-----------------------------------------------------------------------------
    C Interface Implementation
-----------------------------------------------------------------------------*/
extern "C" {
KWXAPP_API int kwxApp_Initialize(int argc, char **argv) {
  if (g_initialized) {
    return 1; // Already initialized
  }

#ifdef _WIN32
  // Activate this DLL's embedded manifest (resource ID 2 = RT_MANIFEST)
  // so that Common Controls v6 visual styles work when loaded by a script
  // host (Deno) whose EXE does not have a manifest.
  if (g_actCtx == INVALID_HANDLE_VALUE) {
    HMODULE hModule = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&kwxApp_Initialize), &hModule);

    ACTCTXW actCtx = {sizeof(ACTCTXW)};
    actCtx.dwFlags =
        ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_HMODULE_VALID;
    actCtx.hModule = hModule;
    actCtx.lpResourceName = MAKEINTRESOURCE(2);
    g_actCtx = CreateActCtxW(&actCtx);
    if (g_actCtx != INVALID_HANDLE_VALUE)
      ActivateActCtx(g_actCtx, &g_actCtxCookie);
  }
#endif

  // wxWidgets requires valid argc/argv
  static char *dummy_argv[] = {const_cast<char *>("kwxApp"), nullptr};
  if (argc <= 0 || argv == nullptr) {
    argc = 1;
    argv = dummy_argv;
  }

  if (!wxEntryStart(argc, argv)) {
    return 0;
  }

  if (!wxTheApp || !wxTheApp->CallOnInit()) {
    wxEntryCleanup();
    return 0;
  }

  g_initialized = true;
  return 1;
}

KWXAPP_API int kwxApp_MainLoop() {
  if (!g_initialized || !wxTheApp) {
    return -1;
  }
  return wxTheApp->OnRun();
}

KWXAPP_API void kwxApp_ExitMainLoop() {
  if (wxTheApp) {
    wxTheApp->ExitMainLoop();
  }
}

KWXAPP_API void kwxApp_Shutdown() {
  if (g_initialized) {
    g_terminating = true;
    if (wxTheApp) {
      wxTheApp->OnExit();
    }
    wxEntryCleanup();
    g_initialized = false;

#ifdef _WIN32
    if (g_actCtx != INVALID_HANDLE_VALUE) {
      DeactivateActCtx(0, g_actCtxCookie);
      ReleaseActCtx(g_actCtx);
      g_actCtx = INVALID_HANDLE_VALUE;
    }
#endif
  }
}

KWXAPP_API int kwxApp_IsTerminating() { return g_terminating ? 1 : 0; }

/*-------------------------------------------------------------------------
    Application Properties
-------------------------------------------------------------------------*/
KWXAPP_API const char *kwxApp_GetAppName() {
  if (!wxTheApp)
    return "";
  g_cachedAppName = wxTheApp->GetAppName();
  return g_cachedAppName.utf8_str();
}

KWXAPP_API void kwxApp_SetAppName(const char *name) {
  if (wxTheApp && name) {
    wxTheApp->SetAppName(wxString::FromUTF8(name));
  }
}

KWXAPP_API const char *kwxApp_GetVendorName() {
  if (!wxTheApp)
    return "";
  g_cachedVendorName = wxTheApp->GetVendorName();
  return g_cachedVendorName.utf8_str();
}

KWXAPP_API void kwxApp_SetVendorName(const char *name) {
  if (wxTheApp && name) {
    wxTheApp->SetVendorName(wxString::FromUTF8(name));
  }
}

KWXAPP_API void *kwxApp_GetTopWindow() {
  return wxTheApp ? wxTheApp->GetTopWindow() : nullptr;
}

KWXAPP_API void kwxApp_SetTopWindow(void *window) {
  if (wxTheApp) {
    wxTheApp->SetTopWindow(static_cast<wxWindow *>(window));
  }
}

KWXAPP_API void kwxApp_SetExitOnFrameDelete(int flag) {
  if (wxTheApp) {
    wxTheApp->SetExitOnFrameDelete(flag != 0);
  }
}

KWXAPP_API int kwxApp_GetExitOnFrameDelete() {
  return (wxTheApp && wxTheApp->GetExitOnFrameDelete()) ? 1 : 0;
}

/*-------------------------------------------------------------------------
    System Information
-------------------------------------------------------------------------*/
KWXAPP_API void kwxApp_GetDisplaySize(int *width, int *height) {
  wxSize size = wxGetDisplaySize();
  if (width)
    *width = size.GetWidth();
  if (height)
    *height = size.GetHeight();
}

KWXAPP_API void kwxApp_GetMousePosition(int *x, int *y) {
  wxPoint pos = wxGetMousePosition();
  if (x)
    *x = pos.x;
  if (y)
    *y = pos.y;
}

KWXAPP_API int kwxApp_GetOsVersion(int *major, int *minor) {
  return wxGetOsVersion(major, minor);
}

KWXAPP_API char *kwxApp_GetOsDescription() {
  return wxStringToNewCStr(wxGetOsDescription());
}

KWXAPP_API char *kwxApp_GetUserId() { return wxStringToNewCStr(wxGetUserId()); }

KWXAPP_API char *kwxApp_GetUserName() {
  return wxStringToNewCStr(wxGetUserName());
}

/*-------------------------------------------------------------------------
    Event Processing
-------------------------------------------------------------------------*/
KWXAPP_API int kwxApp_Pending() {
  return (wxTheApp && wxTheApp->Pending()) ? 1 : 0;
}

KWXAPP_API void kwxApp_Dispatch() {
  if (wxTheApp) {
    wxTheApp->Dispatch();
  }
}

KWXAPP_API int kwxApp_Yield() { return wxYield() ? 1 : 0; }

KWXAPP_API int kwxApp_SafeYield(void *window) {
  return wxSafeYield(static_cast<wxWindow *>(window)) ? 1 : 0;
}

/*-------------------------------------------------------------------------
    Idle Timer
-------------------------------------------------------------------------*/
KWXAPP_API void kwxApp_SetIdleCallback(int interval_ms,
                                       kwxApp_IdleCallback callback_func,
                                       void *callback_data) {
  g_idleCallback = callback_func;
  g_idleCallbackData = callback_data;

  if (g_idleTimer) {
    if (g_idleTimer->IsRunning()) {
      g_idleTimer->Stop();
    }

    if (callback_func && interval_ms >= KWXAPP_MIN_IDLE_INTERVAL_MS) {
      g_idleTimer->Start(interval_ms, false);
    }
  }
}

KWXAPP_API int kwxApp_GetIdleInterval() {
  if (g_idleTimer && g_idleTimer->IsRunning()) {
    return g_idleTimer->GetInterval();
  }
  return 0;
}

/*-------------------------------------------------------------------------
    Utilities
-------------------------------------------------------------------------*/
KWXAPP_API void kwxApp_InitAllImageHandlers() { wxInitAllImageHandlers(); }

KWXAPP_API void kwxApp_EnableTooltips(int enable) {
  wxToolTip::Enable(enable != 0);
}

KWXAPP_API void kwxApp_SetTooltipDelay(int milliseconds) {
  wxToolTip::SetDelay(milliseconds);
}

KWXAPP_API void kwxApp_Bell() { wxBell(); }

KWXAPP_API void kwxApp_Sleep(int seconds) { wxSleep(seconds); }

KWXAPP_API void kwxApp_MilliSleep(int milliseconds) {
  wxMilliSleep(milliseconds);
}

KWXAPP_API void *kwxApp_FindWindowById(int id, void *parent) {
  return wxWindow::FindWindowById(static_cast<long>(id),
                                  static_cast<wxWindow *>(parent));
}

KWXAPP_API void *kwxApp_FindWindowByLabel(const char *label, void *parent) {
  if (!label)
    return nullptr;
  return wxFindWindowByLabel(wxString::FromUTF8(label),
                             static_cast<wxWindow *>(parent));
}

KWXAPP_API void *kwxApp_FindWindowByName(const char *name, void *parent) {
  if (!name)
    return nullptr;
  return wxFindWindowByName(wxString::FromUTF8(name),
                            static_cast<wxWindow *>(parent));
}

KWXAPP_API void kwxApp_FreeString(char *str) { free(str); }

} // extern "C"
