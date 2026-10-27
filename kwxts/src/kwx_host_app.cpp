/*
 * kwx_ts — wxWidgets application host (Phase 2).
 *
 * Adapts the repository's src/kwxApp.cpp for the statically linked kwx_ts
 * executable. The C interface declared in kwx_host_app.h is identical to the
 * upstream header, so foreign-language callers see the same contract.
 *
 * Differences from the upstream implementation:
 *  - KWXAPP_API is empty (static link, no DLL boundary).
 *  - The Windows activation-context block is gone: the executable embeds its
 *    own Common Controls v6 manifest, so no runtime activation is required.
 *  - The wxApp subclass is named kwxHostApp and overrides Dispatch() so that
 *    pending QuickJS jobs are drained after every dispatched wx event.
 */

#include "kwx_host_app.h"

#include "quickjs_host.h"

#include <wx/cmdline.h>
#include <wx/tooltip.h>
#include <wx/wx.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>

/*-----------------------------------------------------------------------------
    Internal state
-----------------------------------------------------------------------------*/
namespace {
bool kwx_g_initialized = false;
bool kwx_g_terminating = false;

// Idle timer support
kwxApp_IdleCallback kwx_g_idle_callback = nullptr;
void *kwx_g_idle_callback_data = nullptr;

// Cached strings for Get functions (to avoid returning dangling pointers)
wxString kwx_g_cached_app_name;
wxString kwx_g_cached_vendor_name;

// Helper to convert wxString to C string (caller must free)
// kwxApp_FreeString releases these buffers with free(), so malloc is the only
// allocator with a matching deallocator here.
// NOLINTNEXTLINE(cppcoreguidelines-no-malloc)
char *KwxStringToNewCStr(const wxString &text) {
  const char *utf8 = text.utf8_str();
  const std::size_t length = std::strlen(utf8) + 1U;
  char *result = static_cast<char *>(
      std::malloc(length)); // NOLINT(cppcoreguidelines-no-malloc)
  if (result != nullptr) {
    std::memcpy(result, utf8, length);
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
    if (kwx_g_idle_callback != nullptr) {
      kwx_g_idle_callback(kwx_g_idle_callback_data);
    }
  }
};

static kwxIdleTimer *kwx_g_idle_timer = nullptr;

/*-----------------------------------------------------------------------------
    The wxApp subclass (hidden from C callers)
-----------------------------------------------------------------------------*/
class kwxHostApp : public wxApp {
public:
  bool OnInit() override {
    if (!wxApp::OnInit()) {
      return false;
    }
    kwx_g_idle_timer = new kwxIdleTimer();
    return true;
  }

  int OnExit() override {
    kwx_g_terminating = true;
    if (kwx_g_idle_timer != nullptr) {
      kwx_g_idle_timer->Stop();
      delete kwx_g_idle_timer;
      kwx_g_idle_timer = nullptr;
    }
    return wxApp::OnExit();
  }

  // Re-enter QuickJS after every dispatched wx event. This is the bridge
  // that lets a wx callback hand control back to the JS engine so pending
  // jobs (promises, microtasks) make progress while the loop is active.
  //
  // NOTE: on MSW the main loop dispatches through the platform event loop and
  // never through wxApp::Dispatch(), so the drain is hooked into
  // ProcessPendingEvents() too, which the loop does invoke every iteration.
  bool Dispatch() override {
    const bool dispatched = wxApp::Dispatch();
    kwx_host_drain_jobs();
    return dispatched;
  }

  void ProcessPendingEvents() override {
    wxApp::ProcessPendingEvents();
    kwx_host_drain_jobs();
  }

  // Prevent wxApp from failing on foreign language command line args
  void OnInitCmdLine(wxCmdLineParser &parser) override {
    parser.SetCmdLine("");
  }

  bool OnCmdLineParsed([[maybe_unused]] wxCmdLineParser &parser) override {
    return true;
  }
};

// This is the actual wxApp instance
wxIMPLEMENT_APP_NO_MAIN(kwxHostApp);

/*-----------------------------------------------------------------------------
    C Interface Implementation
-----------------------------------------------------------------------------*/
extern "C" {
KWXAPP_API int kwxApp_Initialize(int argc, char **argv) {
  if (kwx_g_initialized) {
    return 1; // Already initialized
  }

  // wxWidgets requires valid argc/argv
  static char *dummy_argv[] = {const_cast<char *>("kwxApp"), nullptr};
  if (argc <= 0 || argv == nullptr) {
    argc = 1;
    argv = dummy_argv;
  }

  if (!wxEntryStart(argc, argv)) {
    return 0;
  }

  if (wxTheApp == nullptr || !wxTheApp->CallOnInit()) {
    wxEntryCleanup();
    return 0;
  }

  kwx_g_initialized = true;
  return 1;
}

KWXAPP_API int kwxApp_MainLoop() {
  if (!kwx_g_initialized || wxTheApp == nullptr) {
    return -1;
  }
  return wxTheApp->OnRun();
}

KWXAPP_API void kwxApp_ExitMainLoop() {
  if (wxTheApp != nullptr) {
    wxTheApp->ExitMainLoop();
  }
}

KWXAPP_API void kwxApp_Shutdown() {
  if (kwx_g_initialized) {
    kwx_g_terminating = true;
    if (wxTheApp != nullptr) {
      wxTheApp->OnExit();
    }
    wxEntryCleanup();
    kwx_g_initialized = false;
  }
}

KWXAPP_API int kwxApp_IsTerminating() { return kwx_g_terminating ? 1 : 0; }

/*-------------------------------------------------------------------------
    Application Properties
-------------------------------------------------------------------------*/
KWXAPP_API const char *kwxApp_GetAppName() {
  if (wxTheApp == nullptr) {
    return "";
  }
  kwx_g_cached_app_name = wxTheApp->GetAppName();
  return kwx_g_cached_app_name.utf8_str();
}

KWXAPP_API void kwxApp_SetAppName(const char *name) {
  if (wxTheApp != nullptr && name != nullptr) {
    wxTheApp->SetAppName(wxString::FromUTF8(name));
  }
}

KWXAPP_API const char *kwxApp_GetVendorName() {
  if (wxTheApp == nullptr) {
    return "";
  }
  kwx_g_cached_vendor_name = wxTheApp->GetVendorName();
  return kwx_g_cached_vendor_name.utf8_str();
}

KWXAPP_API void kwxApp_SetVendorName(const char *name) {
  if (wxTheApp != nullptr && name != nullptr) {
    wxTheApp->SetVendorName(wxString::FromUTF8(name));
  }
}

KWXAPP_API void *kwxApp_GetTopWindow() {
  return (wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr;
}

KWXAPP_API void kwxApp_SetTopWindow(void *window) {
  if (wxTheApp != nullptr) {
    wxTheApp->SetTopWindow(static_cast<wxWindow *>(window));
  }
}

KWXAPP_API void kwxApp_SetExitOnFrameDelete(int flag) {
  if (wxTheApp != nullptr) {
    wxTheApp->SetExitOnFrameDelete(flag != 0);
  }
}

KWXAPP_API int kwxApp_GetExitOnFrameDelete() {
  return (wxTheApp != nullptr && wxTheApp->GetExitOnFrameDelete()) ? 1 : 0;
}

/*-------------------------------------------------------------------------
    System Information
-------------------------------------------------------------------------*/
KWXAPP_API void kwxApp_GetDisplaySize(int *width, int *height) {
  wxSize size = wxGetDisplaySize();
  if (width != nullptr) {
    *width = size.GetWidth();
  }
  if (height != nullptr) {
    *height = size.GetHeight();
  }
}

KWXAPP_API void kwxApp_GetMousePosition(int *x, int *y) {
  wxPoint position = wxGetMousePosition();
  if (x != nullptr) {
    *x = position.x;
  }
  if (y != nullptr) {
    *y = position.y;
  }
}

KWXAPP_API int kwxApp_GetOsVersion(int *major, int *minor) {
  return wxGetOsVersion(major, minor);
}

KWXAPP_API char *kwxApp_GetOsDescription() {
  return KwxStringToNewCStr(wxGetOsDescription());
}

KWXAPP_API char *kwxApp_GetUserId() {
  return KwxStringToNewCStr(wxGetUserId());
}

KWXAPP_API char *kwxApp_GetUserName() {
  return KwxStringToNewCStr(wxGetUserName());
}

/*-------------------------------------------------------------------------
    Event Processing
-------------------------------------------------------------------------*/
KWXAPP_API int kwxApp_Pending() {
  return (wxTheApp != nullptr && wxTheApp->Pending()) ? 1 : 0;
}

KWXAPP_API void kwxApp_Dispatch() {
  if (wxTheApp != nullptr) {
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
  kwx_g_idle_callback = callback_func;
  kwx_g_idle_callback_data = callback_data;

  if (kwx_g_idle_timer != nullptr) {
    if (kwx_g_idle_timer->IsRunning()) {
      kwx_g_idle_timer->Stop();
    }

    if (callback_func != nullptr &&
        interval_ms >= KWXAPP_MIN_IDLE_INTERVAL_MS) {
      kwx_g_idle_timer->Start(interval_ms, false);
    }
  }
}

KWXAPP_API int kwxApp_GetIdleInterval() {
  if (kwx_g_idle_timer != nullptr && kwx_g_idle_timer->IsRunning()) {
    return kwx_g_idle_timer->GetInterval();
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
  if (label == nullptr) {
    return nullptr;
  }
  return wxFindWindowByLabel(wxString::FromUTF8(label),
                             static_cast<wxWindow *>(parent));
}

KWXAPP_API void *kwxApp_FindWindowByName(const char *name, void *parent) {
  if (name == nullptr) {
    return nullptr;
  }
  return wxFindWindowByName(wxString::FromUTF8(name),
                            static_cast<wxWindow *>(parent));
}

KWXAPP_API void kwxApp_FreeString(char *str) {
  free(str);
} // NOLINT(cppcoreguidelines-no-malloc)

} // extern "C"
