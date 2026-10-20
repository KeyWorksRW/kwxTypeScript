/*
 * kwx_ts — hand-written native bindings (Phase 3).
 *
 * The bulk of the `kwxHost` surface is produced by the build-time generator
 * (bindings_gen.cpp). This module owns only the functions that are NOT part of
 * the generated kwxFFI corpus — currently the __kwx_test_after event-loop probe
 * used by tests/loop_spike.js and the host support helpers.
 *
 * kwx_host_register_bindings() (declared in quickjs_host.h) is implemented
 * here. It is called once from kwx_runtime_init() while the `kwxHost` object is
 * still being assembled, so it must not recreate the object.
 */

#ifndef KWX_TS_RUNTIME_BINDINGS_H
#define KWX_TS_RUNTIME_BINDINGS_H

#include "quickjs.h"

// Register the hand-written helpers on an existing `kwxHost` object.
void kwx_runtime_register_helpers(JSContext *ctx, JSValue native_object);

// Free every occupied callback slot. Declared here so quickjs_host.cpp can call
// it during shutdown while the context is still valid.
void kwx_runtime_release_callbacks(JSContext *ctx);

#endif // KWX_TS_RUNTIME_BINDINGS_H
