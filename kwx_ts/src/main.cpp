/*
 * kwx_ts — command-line entry point.
 *
 * Usage: kwxts <file> [args...]
 *
 * Reads the entry file as UTF-8, evaluates it as a global-scope QuickJS script
 * (through kwx_runtime_eval_file, which handles BOM stripping), and returns the
 * resulting exit code.
 */

#include "quickjs_host.h"

#include <wx/debug.h>  // wxDISABLE_ASSERTS_IN_RELEASE_BUILD()
#include <wx/log.h>    // wxDISABLE_DEBUG_LOGGING_IN_RELEASE_BUILD()

#include <cstdio>
#include <print>
#include <string>
#include <vector>

using std::println;

int main(int argc, char** argv)
{
    // Release only: silence wxASSERT/wxFAIL and drop the log level below
    // wxLOG_Debug. Both are no-ops in Debug (they require NDEBUG).
    wxDISABLE_ASSERTS_IN_RELEASE_BUILD();
    wxDISABLE_DEBUG_LOGGING_IN_RELEASE_BUILD();

    kwx_trace_process_start();
    kwx_trace_mark("main-begin");

    if (argc < 2)
    {
        println(stderr, "usage: kwxts <file> [args...]");
        return 2;
    }

    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index)
    {
        arguments.emplace_back(argv[index]);
    }

    const std::string entry_path = arguments[1];
    kwx_runtime_set_args(arguments);

    if (!kwx_runtime_init())
    {
        return 1;
    }

    int exit_code = 0;
    const bool ok = kwx_runtime_eval_file(entry_path, exit_code);

    kwx_runtime_shutdown();

    if (!ok)
    {
        return (exit_code != 0) ? exit_code : 1;
    }

    return exit_code;
}
