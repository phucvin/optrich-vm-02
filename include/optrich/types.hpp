#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace optrich {

class ModuleInstance;
struct HostFuncEntry;

using HandleId = uint32_t;
constexpr HandleId kNullHandle = 0;

// Value types supported by Optrich VM 02.
// Handles and funcrefs are strictly distinct from i32 (unforgeable capabilities).
enum class ValType : uint8_t {
    I32 = 0,
    I64 = 1,
    F32 = 2,
    F64 = 3,
    Handle = 4,    // Dynamic capability handle (readOnly checked at runtime)
    RwHandle = 5,  // Statically read-write handle
    RoHandle = 6,  // Statically read-only handle (writes rejected at compile time)
    FuncRef = 7,   // Unforgeable cross-module function reference
};

inline bool is_handle_type(ValType t) {
    return t == ValType::Handle || t == ValType::RwHandle || t == ValType::RoHandle;
}

// Can a value of type `from` be used where `to` is expected?
inline bool is_type_compatible(ValType from, ValType to) {
    if (from == to) return true;
    // Any specific handle (rw_handle / ro_handle) can upcast to dynamic `handle`.
    if (to == ValType::Handle && is_handle_type(from)) return true;
    return false;
}

inline const char* val_type_name(ValType t) {
    switch (t) {
        case ValType::I32:      return "i32";
        case ValType::I64:      return "i64";
        case ValType::F32:      return "f32";
        case ValType::F64:      return "f64";
        case ValType::Handle:   return "handle";
        case ValType::RwHandle: return "rw_handle";
        case ValType::RoHandle: return "ro_handle";
        case ValType::FuncRef:  return "funcref";
    }
    return "unknown";
}

// Fat handle carried directly on the operand stack & locals for O(1) memory access
// without per-access handle-table lookups.
constexpr uint32_t kFlagValid    = 1u << 0;
constexpr uint32_t kFlagReadOnly = 1u << 1;

struct FatHandle {
    uint8_t* ptr      = nullptr;
    uint32_t byteSize = 0;
    uint32_t flags    = 0;
    HandleId id       = kNullHandle;

    static FatHandle null() { return FatHandle{}; }

    bool is_null() const { return id == kNullHandle || (flags & kFlagValid) == 0; }
    bool is_readonly() const { return (flags & kFlagReadOnly) != 0; }
};

// Resolved callable reference carrying its target ModuleInstance* (or HostFuncEntry*)
// so cross-module direct calls, call_indirect, and call_ref push CallFrames with zero copying.
struct FuncRef {
    ModuleInstance*      instance     = nullptr;
    uint32_t             localFuncIdx = 0;
    const HostFuncEntry* host         = nullptr;

    static FuncRef null() { return FuncRef{}; }

    bool is_null() const { return instance == nullptr && host == nullptr; }
    bool is_host() const { return host != nullptr; }
};

// Tagged stack slot value.
// Carrying `type` makes operand-stack and call-frame GC root scanning 100% precise.
struct Value {
    ValType type = ValType::I32;
    union {
        int32_t   i32;
        int64_t   i64;
        float     f32;
        double    f64;
        FatHandle handle;
        FuncRef   funcref;
    };

    Value() : type(ValType::I32), i64(0) {}

    bool is_handle() const { return is_handle_type(type); }

    static Value from_i32(int32_t v)      { Value val; val.type = ValType::I32;     val.i32 = v; return val; }
    static Value from_u32(uint32_t v)     { Value val; val.type = ValType::I32;     val.i32 = static_cast<int32_t>(v); return val; }
    static Value from_i64(int64_t v)      { Value val; val.type = ValType::I64;     val.i64 = v; return val; }
    static Value from_u64(uint64_t v)     { Value val; val.type = ValType::I64;     val.i64 = static_cast<int64_t>(v); return val; }
    static Value from_f32(float v)        { Value val; val.type = ValType::F32;     val.f32 = v; return val; }
    static Value from_f64(double v)       { Value val; val.type = ValType::F64;     val.f64 = v; return val; }
    static Value from_handle(FatHandle h) { Value val; val.type = ValType::Handle;  val.handle = h; return val; }
    static Value from_funcref(FuncRef f)  { Value val; val.type = ValType::FuncRef; val.funcref = f; return val; }

    static Value zero(ValType t) {
        switch (t) {
            case ValType::I32:      return from_i32(0);
            case ValType::I64:      return from_i64(0);
            case ValType::F32:      return from_f32(0.0f);
            case ValType::F64:      return from_f64(0.0);
            case ValType::Handle:
            case ValType::RwHandle:
            case ValType::RoHandle: {
                Value val;
                val.type = t;
                val.handle = FatHandle::null();
                return val;
            }
            case ValType::FuncRef:  return from_funcref(FuncRef::null());
        }
        return from_i32(0);
    }
};

struct GCStats {
    size_t marked           = 0;
    size_t swept            = 0;
    size_t liveAfter        = 0;
    size_t totalCollections = 0;
};

// Runtime trap exception (bounds violation, write to read-only, div-by-zero, unreachable, etc.)
class Trap : public std::runtime_error {
public:
    explicit Trap(const std::string& msg) : std::runtime_error(msg) {}
};

// Compile-time / validation error (unknown opcode, type mismatch, static ro_handle write, etc.)
class CompileError : public std::runtime_error {
public:
    explicit CompileError(const std::string& msg) : std::runtime_error(msg) {}
};

// Parse error (malformed WAT syntax)
class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
};

// Link error (missing import, signature mismatch during multi-module linking)
class LinkError : public std::runtime_error {
public:
    explicit LinkError(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace optrich
