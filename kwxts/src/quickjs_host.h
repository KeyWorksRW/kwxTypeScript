/*
 * kwx_ts — QuickJS runtime host.
 *
 * Owns the JSRuntime/JSContext lifecycle, the `kwxHost` global object, and the
 * `console` prelude. The runtime plumbing and native print bridge are owned
 * here; the generated bindings and hand-written helpers are registered on the
 * same `kwxHost` object.
 */

#ifndef KWX_TS_QUICKJS_HOST_H
#define KWX_TS_QUICKJS_HOST_H

#include <string>
#include <vector>

#include "quickjs.h"

// Create the runtime, context, native object, and console prelude.
// Returns false if the runtime or context could not be created.
bool kwx_runtime_init();

// Tear down the context and runtime. Safe to call when not initialized.
void kwx_runtime_shutdown();

// The active context, or nullptr when the runtime is not initialized.
JSContext *kwx_runtime_context();

// The active runtime, or nullptr when the runtime is not initialized.
JSRuntime *kwx_runtime_jsruntime();

// Drain all pending JS jobs (promises / module evaluation) to completion.
void kwx_host_drain_jobs();

// Read, evaluate, and drain `path` as a global-scope script.
// Returns false on any failure; `exit_code` receives the process exit code.
// The embedded host runtime is installed and `__kwx_run_main(path)` is invoked
// instead of evaluating the entry directly, so modules load through the loader.
bool kwx_runtime_eval_file(const std::string &path, int &exit_code);

// Record the full argv (program name included) for the runtime to expose later.
void kwx_runtime_set_args(const std::vector<std::string> &args);

// The full argv recorded by kwx_runtime_set_args().
const std::vector<std::string> &kwx_runtime_arguments();

// Record the process exit code requested by JS (kwx/platform exit() /
// __kwx_set_exit_code).
void kwx_runtime_set_exit_code(int code);

// The last exit code recorded by JS; 0 when none was requested.
int kwx_runtime_exit_code();

// Emit a startup timing mark to stderr when KWX_TS_TRACE is set (a no-op
// otherwise). Used by the entry point to bracket process startup.
void kwx_trace_mark(const char *label);

// Emit the `pre-main` trace mark, measuring process creation to entry into
// main(). A no-op unless KWX_TS_TRACE is enabled; on non-Windows platforms it
// is always a no-op. Called as the first statement of main().
void kwx_trace_process_start();

// If the pending exception is the platform exit() sentinel, consume it, record
// its exit code, and return true. Otherwise leave the exception pending and
// return false; the caller must then report it.
bool kwx_host_consume_exit_exception(JSContext *ctx);

// Release every native callback slot while the context is still alive.
// Called from kwx_runtime_shutdown() before the context is freed.
void kwx_host_release_callbacks(JSContext *ctx);

// Extension hook: register additional native functions on the `kwxHost` global
// object. Called once from kwx_runtime_init(), after `__kwx_print` has been
// installed but before the object is published to the global scope.
// Implemented by runtime_bindings.cpp: it registers the generated kwxFFI thunks
// plus the hand-written helpers.
void kwx_host_register_bindings(JSContext *ctx, JSValue native_object);

// Print the pending JS exception (message + `stack` property) to stderr.
void kwx_host_report_exception(JSContext *ctx);

#endif // KWX_TS_QUICKJS_HOST_H
