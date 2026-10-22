/*
 * kwx_ts — wxWidgets application host (Phase 2).
 *
 * C-callable lifecycle API for the standalone kwx_ts executable. This is the
 * kwx_ts-local counterpart of the repository's src/kwxApp.h: the declarations
 * are identical, but KWXAPP_API resolves to nothing because the executable
 * links wxWidgets and kwxFFI statically — there is no DLL boundary to export
 * across.
 */

#ifndef KWX_TS_KWX_HOST_APP_H
#define KWX_TS_KWX_HOST_APP_H

// Statically linked: the kwxApp_* entry points are plain extern "C" symbols.
#define KWXAPP_API

#ifdef __cplusplus
extern "C"
{
#endif

    /*-----------------------------------------------------------------------------
        Application Lifecycle
    -----------------------------------------------------------------------------*/

    /* Initialize wxWidgets. Call once before any other wx functions.
     * argc/argv can be passed from main(), or use 0/NULL if not needed.
     * Returns non-zero on success, zero on failure. */
    KWXAPP_API int kwxApp_Initialize(int argc, char** argv);

    /* Run the main event loop. Blocks until the application exits.
     * Returns the application exit code. */
    KWXAPP_API int kwxApp_MainLoop(void);

    /* Exit the main event loop. Call from an event handler to quit. */
    KWXAPP_API void kwxApp_ExitMainLoop(void);

    /* Shutdown wxWidgets and clean up. Optional - called automatically on exit. */
    KWXAPP_API void kwxApp_Shutdown(void);

    /* Returns non-zero if the application is terminating. */
    KWXAPP_API int kwxApp_IsTerminating(void);

    /*-----------------------------------------------------------------------------
        Application Properties
    -----------------------------------------------------------------------------*/

    /* Get/Set application name (used in dialogs, config files, etc.) */
    KWXAPP_API const char* kwxApp_GetAppName(void);
    KWXAPP_API void kwxApp_SetAppName(const char* name);

    /* Get/Set vendor name */
    KWXAPP_API const char* kwxApp_GetVendorName(void);
    KWXAPP_API void kwxApp_SetVendorName(const char* name);

    /* Get/Set the top-level window */
    KWXAPP_API void* kwxApp_GetTopWindow(void);
    KWXAPP_API void kwxApp_SetTopWindow(void* window);

    /* Control whether app exits when last frame is deleted */
    KWXAPP_API void kwxApp_SetExitOnFrameDelete(int flag);
    KWXAPP_API int kwxApp_GetExitOnFrameDelete(void);

    /*-----------------------------------------------------------------------------
        System Information
    -----------------------------------------------------------------------------*/

    /* Get display size in pixels */
    KWXAPP_API void kwxApp_GetDisplaySize(int* width, int* height);

    /* Get current mouse position */
    KWXAPP_API void kwxApp_GetMousePosition(int* x, int* y);

    /* Get OS version. Returns OS type (e.g., wxOS_WINDOWS_NT).
     * major/minor can be NULL if not needed. */
    KWXAPP_API int kwxApp_GetOsVersion(int* major, int* minor);

    /* Get OS description string. Caller must free the returned string. */
    KWXAPP_API char* kwxApp_GetOsDescription(void);

    /* Get user ID. Caller must free the returned string. */
    KWXAPP_API char* kwxApp_GetUserId(void);

    /* Get user name. Caller must free the returned string. */
    KWXAPP_API char* kwxApp_GetUserName(void);

    /*-----------------------------------------------------------------------------
        Event Processing
    -----------------------------------------------------------------------------*/

    /* Process pending events. Returns non-zero if events were pending. */
    KWXAPP_API int kwxApp_Pending(void);

    /* Dispatch the next event. */
    KWXAPP_API void kwxApp_Dispatch(void);

    /* Yield to allow pending events to be processed.
     * Use SafeYield when called from an event handler. */
    KWXAPP_API int kwxApp_Yield(void);
    KWXAPP_API int kwxApp_SafeYield(void* window);

/*-----------------------------------------------------------------------------
    Idle Timer (for periodic background tasks)
-----------------------------------------------------------------------------*/

/* Minimum allowed idle interval in milliseconds */
#define KWXAPP_MIN_IDLE_INTERVAL_MS 100

    /* Set idle callback interval in milliseconds.
     * Pass 0 to disable. Minimum is KWXAPP_MIN_IDLE_INTERVAL_MS.
     * The callback_func receives callback_data as its argument. */
    typedef void (*kwxApp_IdleCallback)(void* data);
    KWXAPP_API void kwxApp_SetIdleCallback(int interval_ms, kwxApp_IdleCallback callback_func,
                                           void* callback_data);

    /* Get current idle interval, or 0 if disabled. */
    KWXAPP_API int kwxApp_GetIdleInterval(void);

    /*-----------------------------------------------------------------------------
        Utilities
    -----------------------------------------------------------------------------*/

    /* Initialize all image handlers (PNG, JPEG, etc.) */
    KWXAPP_API void kwxApp_InitAllImageHandlers(void);

    /* Enable/disable tooltips globally */
    KWXAPP_API void kwxApp_EnableTooltips(int enable);

    /* Set tooltip delay in milliseconds */
    KWXAPP_API void kwxApp_SetTooltipDelay(int milliseconds);

    /* System bell */
    KWXAPP_API void kwxApp_Bell(void);

    /* Sleep for specified duration */
    KWXAPP_API void kwxApp_Sleep(int seconds);
    KWXAPP_API void kwxApp_MilliSleep(int milliseconds);

    /* Find window by ID, label, or name. Returns window pointer or NULL. */
    KWXAPP_API void* kwxApp_FindWindowById(int id, void* parent);
    KWXAPP_API void* kwxApp_FindWindowByLabel(const char* label, void* parent);
    KWXAPP_API void* kwxApp_FindWindowByName(const char* name, void* parent);

    /* Free a string returned by kwxApp functions */
    KWXAPP_API void kwxApp_FreeString(char* str);

#ifdef __cplusplus
}
#endif

#endif /* KWX_TS_KWX_HOST_APP_H */
