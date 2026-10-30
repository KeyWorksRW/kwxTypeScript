/*
 * kwx_ts — argument/return marshaling helpers for the generated kwxFFI thunks.
 *
 * Every function here is `inline` because this header is included by the very
 * large generated translation unit and by the hand-written runtime bindings.
 *
 * The binding corpus only records "pointer" for every pointer parameter, so a
 * generated thunk never knows the concrete type its kwxFFI prototype declares.
 * KwxArgPointer bridges that gap: it stores the raw `void*` the marshaling
 * helpers produce and converts it to whatever pointer type the real prototype
 * requires — a well-defined static_cast for object pointers, and std::bit_cast
 * for the few function-pointer parameters (static_cast from `void*` to a
 * function pointer is ill-formed).
 *
 * Null native pointers always become JS_NULL, never JS_UNDEFINED: the binding
 * corpus tests pointers with `ptr ? ... : ...`.
 */

#ifndef KWX_TS_KWX_JS_MARSHAL_H
#define KWX_TS_KWX_JS_MARSHAL_H

#include "quickjs.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Raw pointer produced by arg_ptr() / arg_fnptr(). Implicitly converts to the
// pointer type of the kwxFFI parameter it is passed to.
class KwxArgPointer
{
public:
    explicit KwxArgPointer(void* value) noexcept : value_(value) {}

    // Object pointers (including `void*`): static_cast is well defined.
    template <typename ObjectType>
        requires(!std::is_function_v<ObjectType>)
    operator ObjectType*() const noexcept
    {
        return static_cast<ObjectType*>(value_);
    }

    // Function pointers: bit_cast preserves the bits without reinterpret_cast.
    template <typename FunctionType>
        requires(std::is_function_v<FunctionType>)
    operator FunctionType*() const noexcept
    {
        return std::bit_cast<FunctionType*>(value_);
    }

private:
    void* value_;
};

/*
 * Owns the UTF-8 buffers produced when a JS string is passed where a C string
 * pointer is expected (tests/loop_spike.js passes a raw string to
 * wxString_CreateUTF8). One instance lives on the thunk's stack so the buffers
 * are released when the thunk returns, including on the error path.
 */
class KwxArgScope
{
public:
    explicit KwxArgScope(JSContext* ctx) noexcept : ctx_(ctx) {}

    KwxArgScope(const KwxArgScope&) = delete;
    KwxArgScope& operator=(const KwxArgScope&) = delete;
    KwxArgScope(KwxArgScope&&) = delete;
    KwxArgScope& operator=(KwxArgScope&&) = delete;

    ~KwxArgScope()
    {
        for (const char* text: texts_)
        {
            if (text != nullptr)
            {
                JS_FreeCString(ctx_, text);
            }
        }
    }

    void Track(const char* text) noexcept
    {
        if (text == nullptr)
        {
            return;
        }
        for (const char*& slot: texts_)
        {
            if (slot == nullptr)
            {
                slot = text;
                return;
            }
        }
    }

private:
    JSContext* ctx_;
    std::array<const char*, 16> texts_ {};
};

// Convert a JS value to a native pointer. Accepts null, undefined, Number,
// BigInt, and (as a temporary UTF-8 buffer tracked by `scope`) String; returns
// false with a pending JS exception on failure.
[[nodiscard]] inline bool arg_ptr(JSContext* ctx, JSValueConst value, void** out_pointer,
                                  KwxArgScope& scope)
{
    if (JS_IsNull(value) || JS_IsUndefined(value))
    {
        *out_pointer = nullptr;
        return true;
    }

    if (JS_IsString(value))
    {
        const char* text = JS_ToCString(ctx, value);
        if (text == nullptr)
        {
            return false;
        }
        scope.Track(text);
        // kwxFFI string parameters are declared TString (char*); the buffer is
        // owned by `scope` and lives until this thunk returns.
        *out_pointer = const_cast<char*>(text);
        return true;
    }

    std::uint64_t raw_value = 0;
    if (JS_IsBigInt(value))
    {
        if (JS_ToBigUint64(ctx, &raw_value, value) < 0)
        {
            return false;
        }
    }
    else
    {
        std::int32_t narrow_value = 0;
        if (JS_ToInt32(ctx, &narrow_value, value) < 0)
        {
            return false;
        }
        raw_value = static_cast<std::uint64_t>(static_cast<std::uint32_t>(narrow_value));
    }

    *out_pointer = std::bit_cast<void*>(static_cast<std::uintptr_t>(raw_value));
    return true;
}

// kwxFFI closure parameters are declared TClosureFun, which kwx_types.h defines
// as `void*`, so a function parameter marshals exactly like a pointer.
[[nodiscard]] inline bool arg_fnptr(JSContext* ctx, JSValueConst value, void** out_pointer,
                                    KwxArgScope& scope)
{
    return arg_ptr(ctx, value, out_pointer, scope);
}

[[nodiscard]] inline bool arg_i32(JSContext* ctx, JSValueConst value, std::int32_t* out_value)
{
    return JS_ToInt32(ctx, out_value, value) == 0;
}

[[nodiscard]] inline bool arg_u32(JSContext* ctx, JSValueConst value, std::uint32_t* out_value)
{
    return JS_ToUint32(ctx, out_value, value) == 0;
}

[[nodiscard]] inline bool arg_u8(JSContext* ctx, JSValueConst value, std::uint8_t* out_value)
{
    std::uint32_t wide_value = 0;
    if (JS_ToUint32(ctx, &wide_value, value) < 0)
    {
        return false;
    }
    *out_value = static_cast<std::uint8_t>(wide_value);
    return true;
}

// The FFI surface maps usize to bigint, but a plain Number must be accepted as
// well.
// JS_ToInt64Ext() already accepts both, so one conversion covers both shapes.
[[nodiscard]] inline bool arg_usize(JSContext* ctx, JSValueConst value, std::size_t* out_value)
{
    std::int64_t signed_value = 0;
    if (JS_ToInt64Ext(ctx, &signed_value, value) < 0)
    {
        return false;
    }
    *out_value = static_cast<std::size_t>(signed_value);
    return true;
}

// 64-bit integers marshal as bigint, but a plain Number must be accepted as
// well; JS_ToInt64Ext() already accepts both.
[[nodiscard]] inline bool arg_i64(JSContext* ctx, JSValueConst value, std::int64_t* out_value)
{
    return JS_ToInt64Ext(ctx, out_value, value) == 0;
}

// A bigint is truncated modulo 2^64; a Number goes through the signed
// conversion first so small positive values work without BigInt().
[[nodiscard]] inline bool arg_u64(JSContext* ctx, JSValueConst value, std::uint64_t* out_value)
{
    if (JS_IsBigInt(value))
    {
        return JS_ToBigUint64(ctx, out_value, value) == 0;
    }
    std::int64_t signed_value = 0;
    if (JS_ToInt64Ext(ctx, &signed_value, value) < 0)
    {
        return false;
    }
    *out_value = static_cast<std::uint64_t>(signed_value);
    return true;
}

[[nodiscard]] inline bool arg_f32(JSContext* ctx, JSValueConst value, float* out_value)
{
    double wide_value = 0.0;
    if (JS_ToFloat64(ctx, &wide_value, value) < 0)
    {
        return false;
    }
    *out_value = static_cast<float>(wide_value);
    return true;
}

[[nodiscard]] inline bool arg_f64(JSContext* ctx, JSValueConst value, double* out_value)
{
    return JS_ToFloat64(ctx, out_value, value) == 0;
}

// Null native pointers become JS_NULL so `ptr ? ... : ...` keeps working.
inline JSValue ret_ptr(JSContext* ctx, const void* pointer)
{
    if (pointer == nullptr)
    {
        return JS_NULL;
    }
    return JS_NewBigUint64(ctx, static_cast<std::uint64_t>(std::bit_cast<std::uintptr_t>(pointer)));
}

// The helpers below take the widest signed/unsigned type so a generated thunk
// can pass any C return type (int, long, TBool, ...) without a narrowing
// warning on the platforms where `long` is 64-bit.
inline JSValue ret_i32(JSContext* ctx, std::int64_t value)
{
    return JS_NewInt32(ctx, static_cast<std::int32_t>(value));
}

inline JSValue ret_u32(JSContext* ctx, std::uint64_t value)
{
    return JS_NewUint32(ctx, static_cast<std::uint32_t>(value));
}

inline JSValue ret_u8(JSContext* ctx, std::int64_t value)
{
    return JS_NewInt32(ctx, static_cast<std::int32_t>(value & 0xFF));
}

inline JSValue ret_usize(JSContext* ctx, std::uint64_t value)
{
    return JS_NewBigUint64(ctx, value);
}

inline JSValue ret_i64(JSContext* ctx, std::int64_t value)
{
    return JS_NewBigInt64(ctx, value);
}

inline JSValue ret_u64(JSContext* ctx, std::uint64_t value)
{
    return JS_NewBigUint64(ctx, value);
}

inline JSValue ret_f32(JSContext* ctx, double value)
{
    return JS_NewFloat64(ctx, static_cast<double>(static_cast<float>(value)));
}

inline JSValue ret_f64(JSContext* ctx, double value)
{
    return JS_NewFloat64(ctx, value);
}

#endif  // KWX_TS_KWX_JS_MARSHAL_H
