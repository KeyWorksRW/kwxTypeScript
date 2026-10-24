/*
 * kwx_ts — hand-written native bindings (Phase 3/4/5).
 *
 * Implements the helpers that are NOT part of the generated kwxFFI corpus: the
 * __kwx_test_after() event-loop probe (used by tests/loop_spike.js) and the
 * host support surface consumed by runtime/host_runtime.js and
 * runtime/kwx_platform.js, including __kwx_get_embedded_factory() for the
 * precompiled runtime assets.
 *
 * Everything here is registered on the existing `kwxHost` object, which is
 * created once in quickjs_host.cpp and must never be recreated here.
 */

#include "runtime_bindings.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <wx/timer.h>

#include "bindings_gen.h"
#include "kwx_host_app.h"
#include "kwx_js_marshal.h"
#include "quickjs_host.h"

/* TypeScript -> CommonJS transformer, built from kwx_ts/third_party/
 * swc_transformer as a Rust `staticlib` and linked into this executable (see
 * CMakeLists.txt). It needs no runtime loading: the first transpile calls
 * straight into the linked code. kwx_swc_transform returns a NUL-terminated
 * buffer owned by the caller (release it with kwx_swc_free) or NULL on
 * failure. */
extern "C" {
const char *kwx_swc_version(void);
char *kwx_swc_transform(const char *source, const char *file_name,
                        std::size_t *out_len);
const char *kwx_swc_last_error(void);
void kwx_swc_free(char *ptr);
}

/* QuickJS bytecode for runtime/kwx_runtime.js, generated at build time by qjsc
 * (see CMakeLists.txt) and compiled as C. The bytecode is a small script that
 * publishes the CommonJS factory on globalThis, so the module body itself is
 * never parsed at startup. */
extern const std::uint8_t kwx_runtime_bytecode[];
extern const std::uint32_t kwx_runtime_bytecode_size;

extern const std::uint8_t kwx_platform_bytecode[];
extern const std::uint32_t kwx_platform_bytecode_size;

namespace {

constexpr std::size_t KWX_CALLBACK_SLOT_COUNT = 64U;

#if defined(_WIN32)
constexpr const char *KWX_PLATFORM_OS = "windows";
#elif defined(__APPLE__)
constexpr const char *KWX_PLATFORM_OS = "darwin";
#else
constexpr const char *KWX_PLATFORM_OS = "linux";
#endif

/* Non-null pointers become BigInt; null becomes JS_NULL (never JS_UNDEFINED).
 */
[[nodiscard]] JSValue KwxPointerToValue(JSContext *ctx, const void *pointer) {
  if (pointer == nullptr) {
    return JS_NULL;
  }
  const std::uintptr_t address = std::bit_cast<std::uintptr_t>(pointer);
  return JS_NewBigUint64(ctx, static_cast<std::uint64_t>(address));
}

// Calls the stored JS function from the wx event loop, then releases the
// reference. `delete this` is safe here: wxTimer documents self-deletion from
// Notify() for one-shot timers.
class KwxOneShotTimer : public wxTimer {
public:
  KwxOneShotTimer(JSContext *ctx, JSValue callback)
      : ctx_(ctx), callback_(callback) {}

  void Notify() override {
    JSValueConst arguments[3] = {callback_, JS_NULL, JS_NULL};
    JSValue result = JS_Call(ctx_, callback_, JS_UNDEFINED, 3, arguments);
    if (JS_IsException(result)) {
      if (kwx_host_consume_exit_exception(ctx_)) {
        // A platform exit() sentinel unwound the timer: leave the event loop.
        kwxApp_ExitMainLoop();
      } else {
        kwx_host_report_exception(ctx_);
      }
    }
    JS_FreeValue(ctx_, result);
    JS_FreeValue(ctx_, callback_);
    delete this;
  }

private:
  JSContext *ctx_;
  JSValue callback_;
};

// __kwx_test_after(ms, fn) — schedule `fn` on the wx event loop after `ms`.
JSValue KwxJsTestAfter(JSContext *ctx, [[maybe_unused]] JSValueConst this_val,
                       int argc, JSValueConst *argv) {
  std::int32_t delay_ms = 0;
  if (argc < 2 || JS_ToInt32(ctx, &delay_ms, argv[0]) < 0 ||
      !JS_IsFunction(ctx, argv[1])) {
    return JS_ThrowTypeError(ctx, "__kwx_test_after expects (ms, function)");
  }

  const std::int32_t actual_delay = (delay_ms > 0) ? delay_ms : 1;
  auto *timer = new KwxOneShotTimer(ctx, JS_DupValue(ctx, argv[1]));
  timer->Start(actual_delay, wxTIMER_ONE_SHOT);
  return JS_UNDEFINED;
}

/* -------------------------------------------------------------- host support
 * ---
 */

// __kwx_encode_utf8(text) — UTF-8 bytes as an ArrayBuffer, NUL bytes preserved.
JSValue KwxJsEncodeUtf8(JSContext *ctx, [[maybe_unused]] JSValueConst this_val,
                        int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "__kwx_encode_utf8 expects (text)");
  }

  std::size_t byte_length = 0;
  const char *text = JS_ToCStringLen2(ctx, &byte_length, argv[0], false);
  if (text == nullptr) {
    return JS_EXCEPTION;
  }

  const std::uint8_t *bytes = std::bit_cast<const std::uint8_t *>(text);
  JSValue buffer = JS_NewArrayBufferCopy(ctx, bytes, byte_length);
  JS_FreeCString(ctx, text);
  return buffer;
}

// __kwx_ptr_of(value) — data address of a TypedArray (plus offset) or
// ArrayBuffer.
JSValue KwxJsPtrOf(JSContext *ctx, [[maybe_unused]] JSValueConst this_val,
                   int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "__kwx_ptr_of expects (value)");
  }

  const JSValueConst value = argv[0];

  std::size_t byte_offset = 0;
  std::size_t byte_length = 0;
  std::size_t bytes_per_element = 0;
  JSValue typed_array_buffer = JS_GetTypedArrayBuffer(
      ctx, value, &byte_offset, &byte_length, &bytes_per_element);
  if (!JS_IsException(typed_array_buffer)) {
    std::size_t buffer_size = 0;
    std::uint8_t *data =
        JS_GetArrayBuffer(ctx, &buffer_size, typed_array_buffer);
    JS_FreeValue(ctx, typed_array_buffer);
    if (data == nullptr) {
      return JS_ThrowTypeError(
          ctx, "__kwx_ptr_of: typed array has no backing buffer");
    }
    return KwxPointerToValue(ctx, data + byte_offset);
  }
  // Not a typed array: drop the pending exception and try an ArrayBuffer.
  JS_FreeValue(ctx, JS_GetException(ctx));

  if (!JS_IsArrayBuffer(value)) {
    return JS_ThrowTypeError(
        ctx, "__kwx_ptr_of: expected a TypedArray or ArrayBuffer");
  }

  std::size_t buffer_size = 0;
  std::uint8_t *data = JS_GetArrayBuffer(ctx, &buffer_size, value);
  if (data == nullptr) {
    return JS_ThrowTypeError(ctx, "__kwx_ptr_of: array buffer has no data");
  }
  return KwxPointerToValue(ctx, data);
}

// __kwx_get_cstring(pointer) — NUL-terminated UTF-8 pointer as a JS string.
JSValue KwxJsGetCString(JSContext *ctx, [[maybe_unused]] JSValueConst this_val,
                        int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "__kwx_get_cstring expects (pointer)");
  }

  KwxArgScope scope(ctx);
  void *pointer = nullptr;
  if (!arg_ptr(ctx, argv[0], &pointer, scope)) {
    return JS_EXCEPTION;
  }
  if (pointer == nullptr) {
    return JS_NULL;
  }

  const char *text = static_cast<const char *>(pointer);
  return JS_NewString(ctx, text);
}

// __kwx_get_array_buffer(pointer, byteLength) — non-owning ArrayBuffer view
// over native memory. The
// caller must guarantee the memory outlives the ArrayBuffer; the kwxFFI
// constant table is a process-lifetime static, which is the only current user.
JSValue KwxJsGetArrayBuffer(JSContext *ctx,
                            [[maybe_unused]] JSValueConst this_val, int argc,
                            JSValueConst *argv) {
  if (argc < 2) {
    return JS_ThrowTypeError(
        ctx, "__kwx_get_array_buffer expects (pointer, byteLength)");
  }

  KwxArgScope scope(ctx);
  void *pointer = nullptr;
  if (!arg_ptr(ctx, argv[0], &pointer, scope)) {
    return JS_EXCEPTION;
  }

  std::int64_t byte_length = 0;
  if (JS_ToInt64(ctx, &byte_length, argv[1]) < 0) {
    return JS_EXCEPTION;
  }
  if (pointer == nullptr) {
    return JS_ThrowTypeError(ctx, "__kwx_get_array_buffer: null pointer");
  }
  if (byte_length < 0) {
    return JS_ThrowRangeError(ctx,
                              "__kwx_get_array_buffer: negative byteLength");
  }

  auto *bytes = static_cast<std::uint8_t *>(pointer);
  // max_len 0 marks the buffer non-resizable; any non-zero value would require
  // a realloc callback we deliberately do not provide.
  return JS_NewArrayBuffer(ctx, bytes, static_cast<std::size_t>(byte_length), 0,
                           nullptr, nullptr, false);
}

// Strip a leading UTF-8 BOM so the module loader can wrap the source verbatim.
void KwxStripUtf8Bom(std::string &text) {
  if (text.size() < 3U) {
    return;
  }
  const unsigned char byte0 = static_cast<unsigned char>(text[0]);
  const unsigned char byte1 = static_cast<unsigned char>(text[1]);
  const unsigned char byte2 = static_cast<unsigned char>(text[2]);
  if (byte0 == 0xEFU && byte1 == 0xBBU && byte2 == 0xBFU) {
    text.erase(0, 3);
  }
}

// __kwx_read_file(path) — whole file as a UTF-8 string; throws with the path.
JSValue KwxJsReadFile(JSContext *ctx, [[maybe_unused]] JSValueConst this_val,
                      int argc, JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "__kwx_read_file expects (path)");
  }

  const char *path = JS_ToCString(ctx, argv[0]);
  if (path == nullptr) {
    return JS_EXCEPTION;
  }

  JSValue result = JS_UNDEFINED;
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    result = JS_ThrowTypeError(ctx, "kwx_ts: cannot read '%s'", path);
  } else {
    std::string contents{std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>()};
    KwxStripUtf8Bom(contents);
    result = JS_NewStringLen(ctx, contents.c_str(), contents.size());
  }

  JS_FreeCString(ctx, path);
  return result;
}

// First existing regular file among <candidate>, <candidate>.<ext>, and
// <candidate>/index.<ext>. Empty string when nothing matches.
[[nodiscard]] std::string
KwxResolveModuleFile(const std::filesystem::path &candidate) {
  const std::array<const char *, 6> extensions = {"",    ".ts",  ".tsx",
                                                  ".js", ".mjs", ".cjs"};
  std::error_code error;
  for (const char *extension : extensions) {
    const std::filesystem::path with_extension(candidate.string() + extension);
    if (std::filesystem::is_regular_file(with_extension, error) && !error) {
      return with_extension.string();
    }
    error.clear();
  }

  for (const char *extension : extensions) {
    const std::filesystem::path index_file(candidate.string() + "/index" +
                                           extension);
    if (std::filesystem::is_regular_file(index_file, error) && !error) {
      return index_file.string();
    }
    error.clear();
  }

  return std::string();
}

// __kwx_module_resolve(base_file, specifier) — absolute normalized path or "".
// Uses std::filesystem only, so it behaves identically on every platform.
JSValue KwxJsModuleResolve(JSContext *ctx,
                           [[maybe_unused]] JSValueConst this_val, int argc,
                           JSValueConst *argv) {
  if (argc < 2) {
    return JS_ThrowTypeError(
        ctx, "__kwx_module_resolve expects (baseFile, specifier)");
  }

  const char *base_text = JS_ToCString(ctx, argv[0]);
  if (base_text == nullptr) {
    return JS_EXCEPTION;
  }
  const char *spec_text = JS_ToCString(ctx, argv[1]);
  if (spec_text == nullptr) {
    JS_FreeCString(ctx, base_text);
    return JS_EXCEPTION;
  }

  std::string resolved;
  try {
    const std::filesystem::path spec_path(spec_text);
    std::filesystem::path base_dir;
    if (base_text[0] != '\0') {
      base_dir = std::filesystem::path(base_text).parent_path();
    }

    std::filesystem::path candidate;
    if (spec_path.is_absolute()) {
      candidate = spec_path;
    } else if (base_dir.empty()) {
      candidate = std::filesystem::absolute(spec_path);
    } else {
      candidate = std::filesystem::absolute(base_dir / spec_path);
    }

    resolved = KwxResolveModuleFile(candidate.lexically_normal());
  } catch (const std::filesystem::filesystem_error &) {
    resolved.clear();
  }

  JS_FreeCString(ctx, spec_text);
  JS_FreeCString(ctx, base_text);
  return JS_NewString(ctx, resolved.c_str());
}

// __kwx_get_args() — the full argv as a JS array (program, entry, user args).
JSValue KwxJsGetArgs(JSContext *ctx, [[maybe_unused]] JSValueConst this_val,
                     [[maybe_unused]] int argc,
                     [[maybe_unused]] JSValueConst *argv) {
  const std::vector<std::string> &arguments = kwx_runtime_arguments();
  JSValue array = JS_NewArray(ctx);
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    JSValue item = JS_NewString(ctx, arguments[index].c_str());
    JS_SetPropertyUint32(ctx, array, static_cast<std::uint32_t>(index), item);
  }
  return array;
}

// __kwx_set_exit_code(code) — record the process exit code requested by JS.
JSValue KwxJsSetExitCode(JSContext *ctx, [[maybe_unused]] JSValueConst this_val,
                         int argc, JSValueConst *argv) {
  std::int32_t exit_code = 0;
  if (argc > 0) {
    std::ignore = JS_ToInt32(ctx, &exit_code, argv[0]);
  }
  kwx_runtime_set_exit_code(exit_code);
  return JS_UNDEFINED;
}

struct KwxEmbeddedAsset {
  const std::uint8_t *bytecode;
  std::uint32_t size;
  const char *factory_name;
};

// Asset name → bytecode and the global its factory is published under.
[[nodiscard]] bool KwxFindEmbeddedAsset(std::string_view name,
                                        KwxEmbeddedAsset &asset) {
  if (name == "kwx_runtime") {
    asset = {kwx_runtime_bytecode, kwx_runtime_bytecode_size,
             "__kwx_runtime_factory"};
    return true;
  }
  if (name == "kwx_platform") {
    asset = {kwx_platform_bytecode, kwx_platform_bytecode_size,
             "__kwx_platform_factory"};
    return true;
  }
  return false;
}

// __kwx_get_embedded_factory(name) — the precompiled CommonJS factory for an
// embedded asset. "kwx_runtime" is the binding corpus; "kwx_platform" is the
// FFI surface module. Loading never touches the filesystem and never parses
// source.
JSValue KwxJsEmbeddedFactory(JSContext *ctx,
                             [[maybe_unused]] JSValueConst this_val, int argc,
                             JSValueConst *argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx, "__kwx_get_embedded_factory expects (name)");
  }

  const char *name = JS_ToCString(ctx, argv[0]);
  if (name == nullptr) {
    return JS_EXCEPTION;
  }

  KwxEmbeddedAsset asset{};
  if (!KwxFindEmbeddedAsset(name, asset)) {
    JSValue error =
        JS_ThrowTypeError(ctx, "kwx_ts: unknown embedded asset '%s'", name);
    JS_FreeCString(ctx, name);
    return error;
  }
  JS_FreeCString(ctx, name);

  JSValue compiled =
      JS_ReadObject(ctx, asset.bytecode, static_cast<std::size_t>(asset.size),
                    JS_READ_OBJ_BYTECODE);
  if (JS_IsException(compiled)) {
    return JS_EXCEPTION;
  }

  // JS_EvalFunction takes ownership of the compiled function object. The script
  // only assigns the factory to the global; the assignment is read back below
  // so the result does not depend on script completion-value semantics.
  JSValue evaluated = JS_EvalFunction(ctx, compiled);
  if (JS_IsException(evaluated)) {
    return JS_EXCEPTION;
  }
  JS_FreeValue(ctx, evaluated);

  JSValue global_object = JS_GetGlobalObject(ctx);
  JSValue factory = JS_GetPropertyStr(ctx, global_object, asset.factory_name);
  JS_FreeValue(ctx, global_object);

  if (!JS_IsFunction(ctx, factory)) {
    JS_FreeValue(ctx, factory);
    return JS_ThrowTypeError(ctx, "kwx_ts: embedded %s factory is unavailable",
                             asset.factory_name);
  }

  return factory;
}

// ---------------------------------------------- transpile cache (Phase 6) ---

constexpr std::uint64_t KWX_FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr std::uint64_t KWX_FNV_PRIME = 1099511628211ULL;
constexpr std::size_t KWX_CACHE_MAX_SOURCE = 8U * 1024U * 1024U;

[[nodiscard]] std::uint64_t
KwxFnv1a64(std::uint64_t hash, const std::uint8_t *bytes, std::size_t length) {
  for (std::size_t index = 0; index < length; ++index) {
    hash ^= static_cast<std::uint64_t>(bytes[index]);
    hash *= KWX_FNV_PRIME;
  }
  return hash;
}

// 64-bit FNV-1a over (transformer version, source bytes) as 16 lowercase hex
// chars. The version string is produced by the linked SWC static library, so
// rebuilding against a different swc_core — or changing the transform pipeline
// — invalidates every cached entry without a separate version bump.
[[nodiscard]] std::string KwxSourceHash(const std::string &source) {
  std::uint64_t hash = KWX_FNV_OFFSET_BASIS;

  const char *version = kwx_swc_version();
  if (version != nullptr) {
    const std::string_view version_text(version);
    hash = KwxFnv1a64(hash,
                      std::bit_cast<const std::uint8_t *>(version_text.data()),
                      version_text.size());
  }

  hash = KwxFnv1a64(hash, std::bit_cast<const std::uint8_t *>(source.data()),
                    source.size());

  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%016llx",
                static_cast<unsigned long long>(hash));
  return std::string(buffer);
}

// 16 lowercase hex chars of the FNV-1a hash of the UTF-8 module path, plus
// `.js`: one file per module path, so the cache cannot grow without bound.
[[nodiscard]] std::string KwxCacheFileName(const std::string &file_path) {
  const std::uint64_t hash = KwxFnv1a64(
      KWX_FNV_OFFSET_BASIS,
      std::bit_cast<const std::uint8_t *>(file_path.data()), file_path.size());
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%016llx.js",
                static_cast<unsigned long long>(hash));
  return std::string(buffer);
}

// Caching is on by default; KWX_TS_NO_CACHE set to a non-empty value disables
// it. Resolved once.
[[nodiscard]] bool KwxTranspileCacheEnabled() {
  static const bool enabled = [] {
    const char *disabled = std::getenv("KWX_TS_NO_CACHE");
    return disabled == nullptr || disabled[0] == '\0';
  }();
  return enabled;
}

// Cache directory, resolved at most once. Empty when no location is available.
[[nodiscard]] const std::string &KwxTranspileCacheDir() {
  static const std::string cache_dir = [] {
    const char *override_dir = std::getenv("KWX_TS_CACHE_DIR");
    if (override_dir != nullptr && override_dir[0] != '\0') {
      return std::string(override_dir);
    }
#if defined(_WIN32)
    const char *local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data == nullptr || local_app_data[0] == '\0') {
      return std::string();
    }
    return std::string(local_app_data) + "\\kwx_ts\\transpile";
#else
    const char *xdg_cache = std::getenv("XDG_CACHE_HOME");
    if (xdg_cache != nullptr && xdg_cache[0] != '\0') {
      return std::string(xdg_cache) + "/kwx_ts/transpile";
    }
    const char *home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
      return std::string();
    }
    return std::string(home) + "/.cache/kwx_ts/transpile";
#endif
  }();
  return cache_dir;
}

// Create the cache directory once. Returns false (silent no-op) when caching is
// disabled or the directory cannot be created.
[[nodiscard]] bool KwxEnsureTranspileCache() {
  static const bool ready = [] {
    if (!KwxTranspileCacheEnabled()) {
      return false;
    }
    const std::string &directory = KwxTranspileCacheDir();
    if (directory.empty()) {
      return false;
    }
    std::error_code create_error;
    std::ignore = std::filesystem::create_directories(directory, create_error);
    if (create_error) {
      return false;
    }
    std::error_code check_error;
    return std::filesystem::is_directory(directory, check_error) &&
           !check_error;
  }();
  return ready;
}

[[nodiscard]] long KwxProcessId() {
#if defined(_WIN32)
  return static_cast<long>(::_getpid());
#else
  return static_cast<long>(::getpid());
#endif
}

// Copy argv value as UTF-8 into `out`. On failure clears any pending exception
// (callers treat that as a miss / no-op) and returns false.
[[nodiscard]] bool KwxArgToString(JSContext *ctx, JSValueConst value,
                                  std::string &out) {
  std::size_t length = 0;
  const char *text = JS_ToCStringLen2(ctx, &length, value, false);
  if (text == nullptr) {
    JS_FreeValue(ctx, JS_GetException(ctx));
    return false;
  }
  out.assign(text, length);
  JS_FreeCString(ctx, text);
  return true;
}

// __kwx_swc_transpile(source, filePath) — lower one TypeScript module to
// CommonJS with the SWC transformer linked into the executable. Throws a JS
// error carrying the transformer diagnostic when the module does not parse.
JSValue KwxJsSwcTransform(JSContext *ctx,
                          [[maybe_unused]] JSValueConst this_val, int argc,
                          JSValueConst *argv) {
  if (argc < 2) {
    return JS_ThrowTypeError(ctx,
                             "__kwx_swc_transpile expects (source, filePath)");
  }

  std::string source;
  std::string file_path;
  if (!KwxArgToString(ctx, argv[0], source) ||
      !KwxArgToString(ctx, argv[1], file_path)) {
    return JS_ThrowTypeError(
        ctx, "__kwx_swc_transpile: source and filePath must be strings");
  }

  std::size_t length = 0;
  char *code = kwx_swc_transform(source.c_str(), file_path.c_str(), &length);
  if (code == nullptr) {
    const char *message = kwx_swc_last_error();
    const std::string detail =
        (message != nullptr && message[0] != '\0')
            ? std::string(message)
            : std::string("the TypeScript transformer failed");
    return JS_ThrowInternalError(ctx, "kwx_ts: %s", detail.c_str());
  }

  JSValue result = JS_NewStringLen(ctx, code, length);
  kwx_swc_free(code);
  return result;
}

// __kwx_eval_factory(source, filePath) — compile a CommonJS factory
// expression with the module path as the eval script name, so stack frames
// name the real file instead of "<input>". The caller wraps the CommonJS
// body in a function expression whose prologue occupies one line, so an eval
// line number is one greater than the generated line.
JSValue KwxJsEvalFactory(JSContext *ctx,
                         [[maybe_unused]] JSValueConst this_val, int argc,
                         JSValueConst *argv) {
  if (argc < 2) {
    return JS_ThrowTypeError(ctx,
                             "__kwx_eval_factory expects (source, filePath)");
  }

  std::string source;
  std::string file_path;
  if (!KwxArgToString(ctx, argv[0], source) ||
      !KwxArgToString(ctx, argv[1], file_path)) {
    return JS_ThrowTypeError(
        ctx, "__kwx_eval_factory: source and filePath must be strings");
  }

  return JS_Eval(ctx, source.c_str(), source.size(), file_path.c_str(),
                 JS_EVAL_TYPE_GLOBAL);
}

// __kwx_transpile_cache_get(source, filePath) — cached transpiled code, or
// JS_NULL on any miss. Never throws; every error is treated as a miss.
JSValue KwxJsTranspileCacheGet(JSContext *ctx,
                               [[maybe_unused]] JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  if (argc < 2) {
    return JS_NULL;
  }

  std::string source;
  std::string file_path;
  if (!KwxArgToString(ctx, argv[0], source) ||
      !KwxArgToString(ctx, argv[1], file_path)) {
    return JS_NULL;
  }

  if (source.size() > KWX_CACHE_MAX_SOURCE || !KwxEnsureTranspileCache()) {
    return JS_NULL;
  }

  const std::filesystem::path cache_file =
      std::filesystem::path(KwxTranspileCacheDir()) /
      KwxCacheFileName(file_path);

  std::ifstream input(cache_file, std::ios::binary);
  if (!input.is_open()) {
    return JS_NULL;
  }
  const std::string contents{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};

  const std::size_t newline = contents.find('\n');
  if (newline == std::string::npos) {
    return JS_NULL;
  }

  // Header: "KWC2 <hash> <length>" with single spaces. Parsed by hand rather
  // than via <sstream>, whose iostream machinery is large and unnecessary here.
  const std::string header = contents.substr(0, newline);
  const std::size_t first_space = header.find(' ');
  const std::size_t second_space = (first_space == std::string::npos)
                                       ? std::string::npos
                                       : header.find(' ', first_space + 1U);
  if (first_space == std::string::npos || second_space == std::string::npos) {
    return JS_NULL;
  }
  const std::string version_token = header.substr(0, first_space);
  const std::string hash_token =
      header.substr(first_space + 1U, second_space - first_space - 1U);
  const std::string length_token = header.substr(second_space + 1U);
  if (version_token != "KWC2" || hash_token != KwxSourceHash(source) ||
      length_token != std::to_string(source.size())) {
    return JS_NULL;
  }

  const std::string code = contents.substr(newline + 1);
  return JS_NewStringLen(ctx, code.c_str(), code.size());
}

// __kwx_transpile_cache_put(source, filePath, code) — write the entry
// atomically. Never throws; I/O failures are ignored.
JSValue KwxJsTranspileCachePut(JSContext *ctx,
                               [[maybe_unused]] JSValueConst this_val, int argc,
                               JSValueConst *argv) {
  if (argc < 3) {
    return JS_UNDEFINED;
  }

  std::string source;
  std::string file_path;
  std::string code;
  if (!KwxArgToString(ctx, argv[0], source) ||
      !KwxArgToString(ctx, argv[1], file_path) ||
      !KwxArgToString(ctx, argv[2], code)) {
    return JS_UNDEFINED;
  }

  if (source.size() > KWX_CACHE_MAX_SOURCE || !KwxEnsureTranspileCache()) {
    return JS_UNDEFINED;
  }

  std::string payload = "KWC2 " + KwxSourceHash(source) + ' ' +
                        std::to_string(source.size()) + '\n';
  payload += code;

  const std::filesystem::path cache_file =
      std::filesystem::path(KwxTranspileCacheDir()) /
      KwxCacheFileName(file_path);
  const std::filesystem::path temp_file = std::filesystem::path(
      cache_file.string() + "." + std::to_string(KwxProcessId()) + ".tmp");

  {
    std::ofstream output(temp_file, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      return JS_UNDEFINED;
    }
    output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    output.flush();
    if (!output.good()) {
      output.close();
      std::error_code remove_error;
      std::ignore = std::filesystem::remove(temp_file, remove_error);
      return JS_UNDEFINED;
    }
  }

  std::error_code rename_error;
  std::filesystem::rename(temp_file, cache_file, rename_error);
  std::ignore = rename_error;
  return JS_UNDEFINED;
}

// -------------------------------------------------------------- callbacks ---

struct KwxCallbackSlot {
  JSContext *ctx = nullptr;
  JSValue callback = JS_UNDEFINED;
  bool occupied = false;
};

std::array<KwxCallbackSlot, KWX_CALLBACK_SLOT_COUNT> kwx_g_callback_slots{};

void KwxInvokeCallbackSlot(std::size_t slot, void *closure_fun, void *data,
                           void *event) {
  KwxCallbackSlot &entry = kwx_g_callback_slots[slot];
  JSContext *ctx = entry.ctx;
  if (ctx == nullptr) {
    return;
  }

  JSValue arguments[3] = {KwxPointerToValue(ctx, closure_fun),
                          KwxPointerToValue(ctx, data),
                          KwxPointerToValue(ctx, event)};

  // Dup so a callback that closes itself mid-flight cannot free the function
  // while it is still executing.
  JSValue callback = JS_DupValue(ctx, entry.callback);
  JSValue result = JS_Call(ctx, callback, JS_UNDEFINED, 3, arguments);
  JS_FreeValue(ctx, callback);

  if (JS_IsException(result)) {
    if (kwx_host_consume_exit_exception(ctx)) {
      // A platform exit() sentinel unwound the callback: leave the loop.
      kwxApp_ExitMainLoop();
    } else {
      kwx_host_report_exception(ctx);
    }
  }
  JS_FreeValue(ctx, result);

  for (JSValue &argument : arguments) {
    JS_FreeValue(ctx, argument);
  }
}

template <int Slot>
void KwxCallbackThunk(void *closure_fun, void *data, void *event) {
  KwxInvokeCallbackSlot(static_cast<std::size_t>(Slot), closure_fun, data,
                        event);
}

template <int... Slots>
[[nodiscard]] constexpr std::array<void (*)(void *, void *, void *),
                                   sizeof...(Slots)>
KwxMakeThunkTable(std::integer_sequence<int, Slots...>) {
  return {&KwxCallbackThunk<Slots>...};
}

const std::array<void (*)(void *, void *, void *), KWX_CALLBACK_SLOT_COUNT>
    kwx_g_callback_thunks = KwxMakeThunkTable(
        std::make_integer_sequence<int, KWX_CALLBACK_SLOT_COUNT>{});

// __kwx_callback_create(fn) — reserve a slot and store `fn`; -1 when full.
JSValue KwxJsCallbackCreate(JSContext *ctx,
                            [[maybe_unused]] JSValueConst this_val, int argc,
                            JSValueConst *argv) {
  if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
    return JS_ThrowTypeError(ctx, "__kwx_callback_create expects (function)");
  }

  for (std::size_t slot = 0; slot < kwx_g_callback_slots.size(); ++slot) {
    KwxCallbackSlot &entry = kwx_g_callback_slots[slot];
    if (!entry.occupied) {
      entry.ctx = ctx;
      entry.callback = JS_DupValue(ctx, argv[0]);
      entry.occupied = true;
      return JS_NewInt32(ctx, static_cast<std::int32_t>(slot));
    }
  }

  return JS_NewInt32(ctx, -1);
}

// __kwx_callback_pointer(slot) — address of the slot's native thunk.
JSValue KwxJsCallbackPointer(JSContext *ctx,
                             [[maybe_unused]] JSValueConst this_val, int argc,
                             JSValueConst *argv) {
  std::int32_t slot = 0;
  if (argc < 1 || JS_ToInt32(ctx, &slot, argv[0]) < 0) {
    return JS_ThrowTypeError(ctx, "__kwx_callback_pointer expects (slot)");
  }
  if (slot < 0 ||
      static_cast<std::size_t>(slot) >= kwx_g_callback_slots.size()) {
    return JS_ThrowRangeError(
        ctx, "__kwx_callback_pointer: slot %d is out of range", slot);
  }

  const auto thunk = kwx_g_callback_thunks[static_cast<std::size_t>(slot)];
  return KwxPointerToValue(ctx, std::bit_cast<const void *>(thunk));
}

// __kwx_callback_close(slot) — release the stored function and free the slot.
JSValue KwxJsCallbackClose(JSContext *ctx,
                           [[maybe_unused]] JSValueConst this_val, int argc,
                           JSValueConst *argv) {
  std::int32_t slot = 0;
  if (argc < 1 || JS_ToInt32(ctx, &slot, argv[0]) < 0) {
    return JS_ThrowTypeError(ctx, "__kwx_callback_close expects (slot)");
  }
  if (slot < 0 ||
      static_cast<std::size_t>(slot) >= kwx_g_callback_slots.size()) {
    return JS_ThrowRangeError(
        ctx, "__kwx_callback_close: slot %d is out of range", slot);
  }

  KwxCallbackSlot &entry = kwx_g_callback_slots[static_cast<std::size_t>(slot)];
  if (entry.occupied) {
    JS_FreeValue(ctx, entry.callback);
    entry.callback = JS_UNDEFINED;
    entry.ctx = nullptr;
    entry.occupied = false;
  }
  return JS_UNDEFINED;
}

} // namespace

void kwx_runtime_register_helpers(JSContext *ctx, JSValue native_object) {
  JS_SetPropertyStr(
      ctx, native_object, "__kwx_test_after",
      JS_NewCFunction(ctx, KwxJsTestAfter, "__kwx_test_after", 2));

  JS_SetPropertyStr(ctx, native_object, "__kwx_platform_os",
                    JS_NewString(ctx, KWX_PLATFORM_OS));

  JS_SetPropertyStr(
      ctx, native_object, "__kwx_encode_utf8",
      JS_NewCFunction(ctx, KwxJsEncodeUtf8, "__kwx_encode_utf8", 1));
  JS_SetPropertyStr(ctx, native_object, "__kwx_ptr_of",
                    JS_NewCFunction(ctx, KwxJsPtrOf, "__kwx_ptr_of", 1));
  JS_SetPropertyStr(
      ctx, native_object, "__kwx_get_cstring",
      JS_NewCFunction(ctx, KwxJsGetCString, "__kwx_get_cstring", 1));
  JS_SetPropertyStr(
      ctx, native_object, "__kwx_get_array_buffer",
      JS_NewCFunction(ctx, KwxJsGetArrayBuffer, "__kwx_get_array_buffer", 2));
  JS_SetPropertyStr(ctx, native_object, "__kwx_read_file",
                    JS_NewCFunction(ctx, KwxJsReadFile, "__kwx_read_file", 1));
  JS_SetPropertyStr(
      ctx, native_object, "__kwx_module_resolve",
      JS_NewCFunction(ctx, KwxJsModuleResolve, "__kwx_module_resolve", 2));
  JS_SetPropertyStr(ctx, native_object, "__kwx_get_args",
                    JS_NewCFunction(ctx, KwxJsGetArgs, "__kwx_get_args", 0));
  JS_SetPropertyStr(
      ctx, native_object, "__kwx_set_exit_code",
      JS_NewCFunction(ctx, KwxJsSetExitCode, "__kwx_set_exit_code", 1));
  JS_SetPropertyStr(ctx, native_object, "__kwx_get_embedded_factory",
                    JS_NewCFunction(ctx, KwxJsEmbeddedFactory,
                                    "__kwx_get_embedded_factory", 1));

  JS_SetPropertyStr(
      ctx, native_object, "__kwx_swc_transpile",
      JS_NewCFunction(ctx, KwxJsSwcTransform, "__kwx_swc_transpile", 2));
  JS_SetPropertyStr(ctx, native_object, "__kwx_transpile_cache_get",
                    JS_NewCFunction(ctx, KwxJsTranspileCacheGet,
                                    "__kwx_transpile_cache_get", 2));
  JS_SetPropertyStr(ctx, native_object, "__kwx_transpile_cache_put",
                    JS_NewCFunction(ctx, KwxJsTranspileCachePut,
                                    "__kwx_transpile_cache_put", 3));
  JS_SetPropertyStr(
      ctx, native_object, "__kwx_eval_factory",
      JS_NewCFunction(ctx, KwxJsEvalFactory, "__kwx_eval_factory", 2));

  JS_SetPropertyStr(
      ctx, native_object, "__kwx_callback_create",
      JS_NewCFunction(ctx, KwxJsCallbackCreate, "__kwx_callback_create", 1));
  JS_SetPropertyStr(
      ctx, native_object, "__kwx_callback_pointer",
      JS_NewCFunction(ctx, KwxJsCallbackPointer, "__kwx_callback_pointer", 1));
  JS_SetPropertyStr(
      ctx, native_object, "__kwx_callback_close",
      JS_NewCFunction(ctx, KwxJsCallbackClose, "__kwx_callback_close", 1));
}

void kwx_runtime_release_callbacks(JSContext *ctx) {
  for (KwxCallbackSlot &entry : kwx_g_callback_slots) {
    if (entry.occupied) {
      JS_FreeValue(ctx, entry.callback);
      entry.callback = JS_UNDEFINED;
      entry.ctx = nullptr;
      entry.occupied = false;
    }
  }
}

void kwx_host_register_bindings(JSContext *ctx, JSValue native_object) {
  kwx_register_generated_bindings(ctx, native_object);
  kwx_runtime_register_helpers(ctx, native_object);
}
