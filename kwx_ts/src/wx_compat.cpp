/*
 * kwx_ts — compatibility definitions for gaps in the current kwxFFI and
 * wxWidgets 3.3.5 trees.
 *
 * kwxFFI (PR #59) declares a few exports it does not implement, and
 * wxWidgets 3.3.5 declares a few functions it does not link. The runtime makes
 * every wrapper reachable through the generated corpus, so the linker pulls the
 * kwxFFI objects that reference these symbols and every one of them must have a
 * definition. Delete an entry as soon as the matching upstream definition
 * appears.
 */

#include <wx/app.h>       // wxTheApp
#include <wx/graphics.h>  // wxGraphicsRenderer
#include <wx/grid.h>      // wxGridCellAutoWrapStringEditor (pulls generic/grideditors.h)
#include <wx/platinfo.h>  // wxPlatformInfo
#include <wx/process.h>   // wxEXEC_SYNC
#include <wx/utils.h>     // wxShell, wxFlushEvents

// Weak definitions let a future wxWidgets or kwxFFI build provide the real
// implementation without a duplicate-symbol error. MSVC has no equivalent, but
// none of these symbols is implemented by the MSVC-linked wxWidgets 3.3.5 build.
#if defined(__GNUC__) && !defined(_MSC_VER)
    #define KWX_COMPAT_WEAK __attribute__((weak))
#else
    #define KWX_COMPAT_WEAK
#endif

// ---------------------------------------------------------------------------
// wxWidgets 3.3.5: declared in wx/utils.h, but not implemented for MSW.
// ---------------------------------------------------------------------------

#if defined(__WXMSW__)

bool wxShell(const wxString& command, wxArrayString& output)
{
    return wxExecute(command, output, wxEXEC_SYNC) != -1;
}

#endif  // __WXMSW__

// Declared in wx/utils.h, but only the X11 port implements it in 3.3.5.
KWX_COMPAT_WEAK void wxFlushEvents()
{
    if (wxTheApp)
    {
        wxTheApp->ProcessPendingEvents();
    }
}

// Declared in wx/platinfo.h, but no platform provides a definition in 3.3.5.
// Mirrors the string-to-enum logic of wxPlatformInfo::GetArch().
KWX_COMPAT_WEAK wxBitness wxPlatformInfo::GetBitness(const wxString& bitness)
{
    if (bitness.Contains(wxT("32")))
    {
        return wxBITNESS_32;
    }

    if (bitness.Contains(wxT("64")))
    {
        return wxBITNESS_64;
    }

    return wxBITNESS_INVALID;
}

// ---------------------------------------------------------------------------
// kwxFFI PR #59: declared in kwx_classes_sync.h / kwx_grid_extra.h, but the
// implementing wrappers were never added to src/.
// ---------------------------------------------------------------------------

extern "C"
{
    // wxGraphicsRenderer is abstract, so the default renderer is the only
    // instance a caller can obtain. It is a singleton owned by wxWidgets: the
    // generated Delete() must not be called on the returned pointer.
    KWX_COMPAT_WEAK wxGraphicsRenderer* wxGraphicsRenderer_Create()
    {
        return wxGraphicsRenderer::GetDefaultRenderer();
    }

    KWX_COMPAT_WEAK wxGridCellAutoWrapStringEditor* wxGridCellAutoWrapStringEditor_Ctor()
    {
        return new wxGridCellAutoWrapStringEditor();
    }
}
