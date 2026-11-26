#include "optrich/vm.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace optrich {

namespace {

template <typename T>
inline T wasm_nearest(T x) {
    if (std::isnan(x) || std::isinf(x) || x == T(0)) return x;
    T r = std::round(x);
    // Ties to even: if |x - r| == 0.5 and r is odd, move toward zero by 1
    if (std::fabs(x - r) == T(0.5)) {
        if (std::fmod(r, T(2.0)) != T(0.0)) {
            r = (r > T(0)) ? (r - T(1)) : (r + T(1));
        }
    }
    return r;
}

template <typename T>
inline T wasm_min(T a, T b) {
    if (std::isnan(a) || std::isnan(b)) return std::numeric_limits<T>::quiet_NaN();
    if (a == T(0) && b == T(0)) return (std::signbit(a) || std::signbit(b)) ? -T(0) : T(0);
    return (a < b) ? a : b;
}

template <typename T>
inline T wasm_max(T a, T b) {
    if (std::isnan(a) || std::isnan(b)) return std::numeric_limits<T>::quiet_NaN();
    if (a == T(0) && b == T(0)) return (std::signbit(a) && std::signbit(b)) ? -T(0) : T(0);
    return (a > b) ? a : b;
}

inline int32_t trunc_f32_s(float v) {
    if (std::isnan(v)) throw Trap("invalid conversion to integer (NaN)");
    if (v < -2147483648.0f || v >= 2147483648.0f) throw Trap("integer overflow");
    return static_cast<int32_t>(v);
}

inline int32_t trunc_f32_u(float v) {
    if (std::isnan(v)) throw Trap("invalid conversion to integer (NaN)");
    if (v <= -1.0f || v >= 4294967296.0f) throw Trap("integer overflow");
    return static_cast<int32_t>(static_cast<uint32_t>(v));
}

inline int32_t trunc_f64_s(double v) {
    if (std::isnan(v)) throw Trap("invalid conversion to integer (NaN)");
    if (v <= -2147483649.0 || v >= 2147483648.0) throw Trap("integer overflow");
    return static_cast<int32_t>(v);
}

inline int32_t trunc_f64_u(double v) {
    if (std::isnan(v)) throw Trap("invalid conversion to integer (NaN)");
    if (v <= -1.0 || v >= 4294967296.0) throw Trap("integer overflow");
    return static_cast<int32_t>(static_cast<uint32_t>(v));
}

inline int64_t trunc_f32_i64_s(float v) {
    if (std::isnan(v)) throw Trap("invalid conversion to integer (NaN)");
    if (v < -9223372036854775808.0f || v >= 9223372036854775808.0f) throw Trap("integer overflow");
    return static_cast<int64_t>(v);
}

inline int64_t trunc_f32_i64_u(float v) {
    if (std::isnan(v)) throw Trap("invalid conversion to integer (NaN)");
    if (v <= -1.0f || v >= 18446744073709551616.0f) throw Trap("integer overflow");
    return static_cast<int64_t>(static_cast<uint64_t>(v));
}

inline int64_t trunc_f64_i64_s(double v) {
    if (std::isnan(v)) throw Trap("invalid conversion to integer (NaN)");
    if (v < -9223372036854775808.0 || v >= 9223372036854775808.0) throw Trap("integer overflow");
    return static_cast<int64_t>(v);
}

inline int64_t trunc_f64_i64_u(double v) {
    if (std::isnan(v)) throw Trap("invalid conversion to integer (NaN)");
    if (v <= -1.0 || v >= 18446744073709551616.0) throw Trap("integer overflow");
    return static_cast<int64_t>(static_cast<uint64_t>(v));
}

template <typename T>
inline T fat_read(const FatHandle& h, uint32_t off) {
    if ((h.flags & kFlagValid) == 0) {
        throw Trap("null handle dereference on read");
    }
    if (static_cast<uint64_t>(off) + sizeof(T) > h.byteSize) {
        throw Trap("memory read out of bounds");
    }
    T val;
    std::memcpy(&val, h.ptr + off, sizeof(T));
    return val;
}

template <typename T>
inline void fat_write(const FatHandle& h, uint32_t off, T val) {
    if ((h.flags & kFlagValid) == 0) {
        throw Trap("null handle dereference on write");
    }
    if ((h.flags & kFlagReadOnly) != 0) {
        throw Trap("memory write on read-only handle");
    }
    if (static_cast<uint64_t>(off) + sizeof(T) > h.byteSize) {
        throw Trap("memory write out of bounds");
    }
    std::memcpy(h.ptr + off, &val, sizeof(T));
}

} // namespace

ModuleInstance::ModuleInstance(CompiledModule module, MemoryStore& store)
    : module_(std::move(module)), store_(store) {
    // Pre-allocate all string constants as read-only handles in the shared MemoryStore.
    stringHandles_.reserve(module_.strings.size());
    for (const auto& s : module_.strings) {
        stringHandles_.push_back(store_.alloc_readonly_bytes(s));
    }

    // Initialize globals.
    globals_.reserve(module_.globals.size());
    for (const auto& g : module_.globals) {
        if (g.initStrIdx >= 0) {
            globals_.push_back(Value::from_handle(stringHandles_[static_cast<size_t>(g.initStrIdx)]));
        } else {
            globals_.push_back(g.initVal);
        }
    }
}

uint32_t ModuleInstance::find_export(std::string_view name) const {
    auto it = module_.exports.find(std::string(name));
    if (it == module_.exports.end()) {
        throw Trap("export not found: '" + std::string(name) + "'");
    }
    return it->second;
}

VM::VM(size_t max_stack_values, size_t max_call_depth)
    : maxStack_(max_stack_values), maxCallDepth_(max_call_depth) {
    stack_.resize(maxStack_);
    frames_.reserve(64);
}

std::vector<Value> VM::invoke(ModuleInstance& inst, std::string_view export_name,
                              const std::vector<Value>& args) {
    uint32_t fidx = inst.find_export(export_name);
    return invoke_index(inst, fidx, args);
}

std::vector<Value> VM::invoke_index(ModuleInstance& inst, uint32_t func_idx,
                                    const std::vector<Value>& args) {
    const CompiledModule& mod = inst.module();
    if (func_idx >= mod.funcs.size()) {
        throw Trap("function index out of range");
    }
    const CompiledFunc* func = &mod.funcs[func_idx];
    if (args.size() != func->paramCount) {
        throw Trap("argument count mismatch: expected " + std::to_string(func->paramCount) +
                   ", got " + std::to_string(args.size()));
    }

    frames_.clear();
    Value* const stack_base = stack_.data();
    size_t sp = 0;

    if (func->locals.size() >= maxStack_) {
        throw Trap("stack overflow");
    }

    // Push arguments as initial locals, zero-initialize remaining locals.
    for (size_t i = 0; i < args.size(); ++i) {
        stack_base[sp++] = args[i];
    }
    for (size_t i = func->paramCount; i < func->locals.size(); ++i) {
        stack_base[sp++] = Value::zero(func->locals[i]);
    }

    size_t locals_base = 0;
    Value* locals = stack_base;
    const Instr* pc = func->code.data();
    frames_.push_back(CallFrame{func, pc, locals_base, sp});

    MemoryStore& store = inst.store();
    Value* globals = inst.globals().data();
    const FatHandle* str_handles = inst.string_handles().data();

    auto do_branch = [&](uint32_t target_pc, uint16_t arity, uint32_t drop_count) {
        if (drop_count > 0) {
            if (arity > 0) {
                size_t dst = sp - drop_count - arity;
                size_t src = sp - arity;
                for (uint16_t k = 0; k < arity; ++k) {
                    stack_base[dst + k] = stack_base[src + k];
                }
            }
            sp -= drop_count;
        }
        pc = func->code.data() + target_pc;
    };

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    static const void* kDispatch[static_cast<size_t>(OpCode::Count)] = {
        &&L_Nop,
        &&L_Unreachable,
        &&L_Br,
        &&L_BrIf,
        &&L_BrIfZero,
        &&L_BrTable,
        &&L_Return,
        &&L_Call,
        &&L_Drop,
        &&L_Select,
        &&L_LocalGet,
        &&L_LocalSet,
        &&L_LocalTee,
        &&L_GlobalGet,
        &&L_GlobalSet,
        &&L_I32Const,
        &&L_I64Const,
        &&L_F32Const,
        &&L_F64Const,
        &&L_HandleNull,
        &&L_MemConstStr,
        &&L_MemAlloc,
        &&L_MemSpan,
        &&L_MemRefSpan,
        &&L_MemReadOnly,
        &&L_MemByteLen,
        &&L_MemRefLen,
        &&L_MemIsReadOnly,
        &&L_MemIsNull,
        &&L_MemEq,
        &&L_MemReadU8,
        &&L_MemReadI8,
        &&L_MemReadU16,
        &&L_MemReadI16,
        &&L_MemReadI32,
        &&L_MemReadI64,
        &&L_MemReadF32,
        &&L_MemReadF64,
        &&L_MemWriteU8,
        &&L_MemWriteU16,
        &&L_MemWriteI32,
        &&L_MemWriteI64,
        &&L_MemWriteF32,
        &&L_MemWriteF64,
        &&L_MemReadRef,
        &&L_MemWriteRef,
        &&L_MemCopy,
        &&L_MemFill,
        &&L_MemCmp,
        &&L_I32Eqz,
        &&L_I32Eq, &&L_I32Ne, &&L_I32LtS, &&L_I32LtU, &&L_I32GtS, &&L_I32GtU,
        &&L_I32LeS, &&L_I32LeU, &&L_I32GeS, &&L_I32GeU,
        &&L_I32Clz, &&L_I32Ctz, &&L_I32Popcnt,
        &&L_I32Add, &&L_I32Sub, &&L_I32Mul, &&L_I32DivS, &&L_I32DivU, &&L_I32RemS, &&L_I32RemU,
        &&L_I32And, &&L_I32Or, &&L_I32Xor, &&L_I32Shl, &&L_I32ShrS, &&L_I32ShrU, &&L_I32Rotl, &&L_I32Rotr,
        &&L_I64Eqz,
        &&L_I64Eq, &&L_I64Ne, &&L_I64LtS, &&L_I64LtU, &&L_I64GtS, &&L_I64GtU,
        &&L_I64LeS, &&L_I64LeU, &&L_I64GeS, &&L_I64GeU,
        &&L_I64Clz, &&L_I64Ctz, &&L_I64Popcnt,
        &&L_I64Add, &&L_I64Sub, &&L_I64Mul, &&L_I64DivS, &&L_I64DivU, &&L_I64RemS, &&L_I64RemU,
        &&L_I64And, &&L_I64Or, &&L_I64Xor, &&L_I64Shl, &&L_I64ShrS, &&L_I64ShrU, &&L_I64Rotl, &&L_I64Rotr,
        &&L_F32Eq, &&L_F32Ne, &&L_F32Lt, &&L_F32Gt, &&L_F32Le, &&L_F32Ge,
        &&L_F32Abs, &&L_F32Neg, &&L_F32Ceil, &&L_F32Floor, &&L_F32Trunc, &&L_F32Nearest, &&L_F32Sqrt,
        &&L_F32Add, &&L_F32Sub, &&L_F32Mul, &&L_F32Div, &&L_F32Min, &&L_F32Max, &&L_F32Copysign,
        &&L_F64Eq, &&L_F64Ne, &&L_F64Lt, &&L_F64Gt, &&L_F64Le, &&L_F64Ge,
        &&L_F64Abs, &&L_F64Neg, &&L_F64Ceil, &&L_F64Floor, &&L_F64Trunc, &&L_F64Nearest, &&L_F64Sqrt,
        &&L_F64Add, &&L_F64Sub, &&L_F64Mul, &&L_F64Div, &&L_F64Min, &&L_F64Max, &&L_F64Copysign,
        &&L_I32WrapI64,
        &&L_I64ExtendI32S, &&L_I64ExtendI32U,
        &&L_I32TruncF32S, &&L_I32TruncF32U, &&L_I32TruncF64S, &&L_I32TruncF64U,
        &&L_I64TruncF32S, &&L_I64TruncF32U, &&L_I64TruncF64S, &&L_I64TruncF64U,
        &&L_F32ConvertI32S, &&L_F32ConvertI32U, &&L_F32ConvertI64S, &&L_F32ConvertI64U,
        &&L_F64ConvertI32S, &&L_F64ConvertI32U, &&L_F64ConvertI64S, &&L_F64ConvertI64U,
        &&L_F32DemoteF64, &&L_F64PromoteF32,
        &&L_I32ReinterpretF32, &&L_I64ReinterpretF64, &&L_F32ReinterpretI32, &&L_F64ReinterpretI64,
        &&L_I32Extend8S, &&L_I32Extend16S, &&L_I64Extend8S, &&L_I64Extend16S, &&L_I64Extend32S,
    };

    const Instr* ins;
    #define DISPATCH() do { ins = pc++; goto *kDispatch[static_cast<size_t>(ins->op)]; } while (0)
    #define CASE_OP(name) L_##name:
    DISPATCH();
#else
    for (;;) {
        const Instr* ins = pc++;
        switch (ins->op) {
    #define DISPATCH() continue
    #define CASE_OP(name) case OpCode::name:
#endif

    CASE_OP(Nop)
        DISPATCH();

    CASE_OP(Unreachable)
        throw Trap("unreachable executed");

    CASE_OP(Br)
        do_branch(ins->imm.u32, ins->aux, ins->aux2);
        DISPATCH();

    CASE_OP(BrIf) {
        int32_t cond = stack_base[--sp].i32;
        if (cond != 0) {
            do_branch(ins->imm.u32, ins->aux, ins->aux2);
        }
        DISPATCH();
    }

    CASE_OP(BrIfZero) {
        int32_t cond = stack_base[--sp].i32;
        if (cond == 0) {
            do_branch(ins->imm.u32, ins->aux, ins->aux2);
        }
        DISPATCH();
    }

    CASE_OP(BrTable) {
        uint32_t idx = static_cast<uint32_t>(stack_base[--sp].i32);
        const BrTable& bt = func->brTables[ins->imm.u32];
        const BrTableTarget& t = (idx < bt.targets.size()) ? bt.targets[idx] : bt.defaultTarget;
        do_branch(t.targetPc, t.arity, t.dropCount);
        DISPATCH();
    }

    CASE_OP(Return) {
        size_t res_count = func->sig.results.size();
        size_t ret_dst = locals_base;
        if (res_count > 0) {
            size_t res_src = sp - res_count;
            for (size_t k = 0; k < res_count; ++k) {
                stack_base[ret_dst + k] = stack_base[res_src + k];
            }
        }
        sp = ret_dst + res_count;
        frames_.pop_back();
        if (frames_.empty()) {
            std::vector<Value> results(res_count);
            for (size_t k = 0; k < res_count; ++k) {
                results[k] = stack_base[k];
            }
            return results;
        }
        const CallFrame& prev = frames_.back();
        func = prev.func;
        pc = prev.pc;
        locals_base = prev.localsBase;
        locals = stack_base + locals_base;
        DISPATCH();
    }

    CASE_OP(Call) {
        if (frames_.size() >= maxCallDepth_) {
            throw Trap("call stack overflow");
        }
        // Save caller's resume PC
        frames_.back().pc = pc;

        const CompiledFunc* callee = &mod.funcs[ins->imm.u32];
        size_t new_locals_base = sp - callee->paramCount;
        size_t extra_locals = callee->locals.size() - callee->paramCount;
        if (sp + extra_locals + 64 >= maxStack_) {
            throw Trap("operand stack overflow");
        }
        for (size_t k = callee->paramCount; k < callee->locals.size(); ++k) {
            stack_base[sp++] = Value::zero(callee->locals[k]);
        }
        func = callee;
        pc = callee->code.data();
        locals_base = new_locals_base;
        locals = stack_base + locals_base;
        frames_.push_back(CallFrame{func, pc, locals_base, sp});
        DISPATCH();
    }

    CASE_OP(Drop)
        --sp;
        DISPATCH();

    CASE_OP(Select) {
        int32_t cond = stack_base[--sp].i32;
        Value v2 = stack_base[--sp];
        if (cond == 0) {
            stack_base[sp - 1] = v2;
        }
        DISPATCH();
    }

    CASE_OP(LocalGet)
        stack_base[sp++] = locals[ins->imm.u32];
        DISPATCH();

    CASE_OP(LocalSet)
        locals[ins->imm.u32] = stack_base[--sp];
        DISPATCH();

    CASE_OP(LocalTee)
        locals[ins->imm.u32] = stack_base[sp - 1];
        DISPATCH();

    CASE_OP(GlobalGet)
        stack_base[sp++] = globals[ins->imm.u32];
        DISPATCH();

    CASE_OP(GlobalSet)
        globals[ins->imm.u32] = stack_base[--sp];
        DISPATCH();

    CASE_OP(I32Const)
        stack_base[sp++].i32 = ins->imm.i32;
        DISPATCH();

    CASE_OP(I64Const)
        stack_base[sp++].i64 = ins->imm.i64;
        DISPATCH();

    CASE_OP(F32Const)
        stack_base[sp++].f32 = ins->imm.f32;
        DISPATCH();

    CASE_OP(F64Const)
        stack_base[sp++].f64 = ins->imm.f64;
        DISPATCH();

    CASE_OP(HandleNull)
        stack_base[sp++].handle = FatHandle::null();
        DISPATCH();

    CASE_OP(MemConstStr)
        stack_base[sp++].handle = str_handles[ins->imm.u32];
        DISPATCH();

    // --- Native Split-Block Memory Opcodes ---

    CASE_OP(MemAlloc) {
        int32_t num_refs  = stack_base[--sp].i32;
        int32_t num_bytes = stack_base[--sp].i32;
        if (num_bytes < 0 || num_refs < 0) {
            throw Trap("mem.alloc negative size");
        }
        stack_base[sp++].handle = store.alloc(static_cast<uint32_t>(num_bytes),
                                              static_cast<uint32_t>(num_refs));
        DISPATCH();
    }

    CASE_OP(MemSpan) {
        int32_t byte_len = stack_base[--sp].i32;
        int32_t byte_off = stack_base[--sp].i32;
        FatHandle h      = stack_base[--sp].handle;
        if (byte_off < 0 || byte_len < 0) {
            throw Trap("mem.span negative offset or length");
        }
        stack_base[sp++].handle = store.make_span(h.id, static_cast<uint32_t>(byte_off),
                                                  static_cast<uint32_t>(byte_len));
        DISPATCH();
    }

    CASE_OP(MemRefSpan) {
        int32_t ref_len = stack_base[--sp].i32;
        int32_t ref_off = stack_base[--sp].i32;
        FatHandle h     = stack_base[--sp].handle;
        if (ref_off < 0 || ref_len < 0) {
            throw Trap("mem.ref_span negative offset or length");
        }
        stack_base[sp++].handle = store.make_ref_span(h.id, static_cast<uint32_t>(ref_off),
                                                      static_cast<uint32_t>(ref_len));
        DISPATCH();
    }

    CASE_OP(MemReadOnly) {
        FatHandle h = stack_base[sp - 1].handle;
        stack_base[sp - 1].handle = store.as_readonly(h.id);
        DISPATCH();
    }

    CASE_OP(MemByteLen) {
        FatHandle h = stack_base[sp - 1].handle;
        if ((h.flags & kFlagValid) == 0) {
            throw Trap("mem.byte_len on null handle");
        }
        stack_base[sp - 1].i32 = static_cast<int32_t>(h.byteSize);
        DISPATCH();
    }

    CASE_OP(MemRefLen) {
        FatHandle h = stack_base[sp - 1].handle;
        stack_base[sp - 1].i32 = static_cast<int32_t>(store.ref_len(h.id));
        DISPATCH();
    }

    CASE_OP(MemIsReadOnly) {
        FatHandle h = stack_base[sp - 1].handle;
        if ((h.flags & kFlagValid) == 0) {
            throw Trap("mem.is_readonly on null handle");
        }
        stack_base[sp - 1].i32 = ((h.flags & kFlagReadOnly) != 0) ? 1 : 0;
        DISPATCH();
    }

    CASE_OP(MemIsNull) {
        FatHandle h = stack_base[sp - 1].handle;
        stack_base[sp - 1].i32 = h.is_null() ? 1 : 0;
        DISPATCH();
    }

    CASE_OP(MemEq) {
        FatHandle b = stack_base[--sp].handle;
        FatHandle a = stack_base[sp - 1].handle;
        stack_base[sp - 1].i32 = (a.id == b.id) ? 1 : 0;
        DISPATCH();
    }

    // --- Fast-path Typed Byte Reads (zero handle-table lookup) ---

    CASE_OP(MemReadU8) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        stack_base[sp - 1].i32 = static_cast<int32_t>(fat_read<uint8_t>(h, off));
        DISPATCH();
    }

    CASE_OP(MemReadI8) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        stack_base[sp - 1].i32 = static_cast<int32_t>(fat_read<int8_t>(h, off));
        DISPATCH();
    }

    CASE_OP(MemReadU16) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        stack_base[sp - 1].i32 = static_cast<int32_t>(fat_read<uint16_t>(h, off));
        DISPATCH();
    }

    CASE_OP(MemReadI16) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        stack_base[sp - 1].i32 = static_cast<int32_t>(fat_read<int16_t>(h, off));
        DISPATCH();
    }

    CASE_OP(MemReadI32) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        stack_base[sp - 1].i32 = fat_read<int32_t>(h, off);
        DISPATCH();
    }

    CASE_OP(MemReadI64) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        stack_base[sp - 1].i64 = fat_read<int64_t>(h, off);
        DISPATCH();
    }

    CASE_OP(MemReadF32) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        stack_base[sp - 1].f32 = fat_read<float>(h, off);
        DISPATCH();
    }

    CASE_OP(MemReadF64) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        stack_base[sp - 1].f64 = fat_read<double>(h, off);
        DISPATCH();
    }

    // --- Fast-path Typed Byte Writes (zero handle-table lookup) ---

    CASE_OP(MemWriteU8) {
        uint8_t val  = static_cast<uint8_t>(stack_base[--sp].i32);
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[--sp].handle;
        fat_write<uint8_t>(h, off, val);
        DISPATCH();
    }

    CASE_OP(MemWriteU16) {
        uint16_t val = static_cast<uint16_t>(stack_base[--sp].i32);
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[--sp].handle;
        fat_write<uint16_t>(h, off, val);
        DISPATCH();
    }

    CASE_OP(MemWriteI32) {
        int32_t val  = stack_base[--sp].i32;
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[--sp].handle;
        fat_write<int32_t>(h, off, val);
        DISPATCH();
    }

    CASE_OP(MemWriteI64) {
        int64_t val  = stack_base[--sp].i64;
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[--sp].handle;
        fat_write<int64_t>(h, off, val);
        DISPATCH();
    }

    CASE_OP(MemWriteF32) {
        float val    = stack_base[--sp].f32;
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[--sp].handle;
        fat_write<float>(h, off, val);
        DISPATCH();
    }

    CASE_OP(MemWriteF64) {
        double val   = stack_base[--sp].f64;
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[--sp].handle;
        fat_write<double>(h, off, val);
        DISPATCH();
    }

    // --- Reference Slot Ops ---

    CASE_OP(MemReadRef) {
        uint32_t slot = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h   = stack_base[sp - 1].handle;
        stack_base[sp - 1].handle = store.read_ref(h.id, slot);
        DISPATCH();
    }

    CASE_OP(MemWriteRef) {
        FatHandle child = stack_base[--sp].handle;
        uint32_t slot   = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h     = stack_base[--sp].handle;
        store.write_ref(h.id, slot, child.id);
        DISPATCH();
    }

    // --- Bulk Memory Ops ---

    CASE_OP(MemCopy) {
        uint32_t len     = static_cast<uint32_t>(stack_base[--sp].i32);
        uint32_t src_off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle src    = stack_base[--sp].handle;
        uint32_t dst_off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle dst    = stack_base[--sp].handle;
        if ((dst.flags & kFlagValid) == 0 || (src.flags & kFlagValid) == 0) {
            throw Trap("mem.copy on null handle");
        }
        if ((dst.flags & kFlagReadOnly) != 0) {
            throw Trap("mem.copy into read-only handle");
        }
        if (static_cast<uint64_t>(dst_off) + len > dst.byteSize ||
            static_cast<uint64_t>(src_off) + len > src.byteSize) {
            throw Trap("mem.copy out of bounds");
        }
        if (len > 0) {
            std::memmove(dst.ptr + dst_off, src.ptr + src_off, len);
        }
        DISPATCH();
    }

    CASE_OP(MemFill) {
        uint32_t len     = static_cast<uint32_t>(stack_base[--sp].i32);
        int32_t  val     = stack_base[--sp].i32;
        uint32_t dst_off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle dst    = stack_base[--sp].handle;
        if ((dst.flags & kFlagValid) == 0) {
            throw Trap("mem.fill on null handle");
        }
        if ((dst.flags & kFlagReadOnly) != 0) {
            throw Trap("mem.fill on read-only handle");
        }
        if (static_cast<uint64_t>(dst_off) + len > dst.byteSize) {
            throw Trap("mem.fill out of bounds");
        }
        if (len > 0) {
            std::memset(dst.ptr + dst_off, val & 0xFF, len);
        }
        DISPATCH();
    }

    CASE_OP(MemCmp) {
        uint32_t len   = static_cast<uint32_t>(stack_base[--sp].i32);
        uint32_t b_off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle b    = stack_base[--sp].handle;
        uint32_t a_off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle a    = stack_base[sp - 1].handle;
        if ((a.flags & kFlagValid) == 0 || (b.flags & kFlagValid) == 0) {
            throw Trap("mem.cmp on null handle");
        }
        if (static_cast<uint64_t>(a_off) + len > a.byteSize ||
            static_cast<uint64_t>(b_off) + len > b.byteSize) {
            throw Trap("mem.cmp out of bounds");
        }
        int cmp = (len == 0) ? 0 : std::memcmp(a.ptr + a_off, b.ptr + b_off, len);
        stack_base[sp - 1].i32 = (cmp < 0) ? -1 : ((cmp > 0) ? 1 : 0);
        DISPATCH();
    }

    // --- i32 ops ---

    CASE_OP(I32Eqz)
        stack_base[sp - 1].i32 = (stack_base[sp - 1].i32 == 0) ? 1 : 0;
        DISPATCH();

    #define I32_BINOP(name, expr) \
        CASE_OP(name) { \
            int32_t b = stack_base[--sp].i32; \
            int32_t a = stack_base[sp - 1].i32; \
            stack_base[sp - 1].i32 = (expr); \
            DISPATCH(); \
        }

    I32_BINOP(I32Eq,  a == b ? 1 : 0)
    I32_BINOP(I32Ne,  a != b ? 1 : 0)
    I32_BINOP(I32LtS, a < b ? 1 : 0)
    I32_BINOP(I32LtU, static_cast<uint32_t>(a) < static_cast<uint32_t>(b) ? 1 : 0)
    I32_BINOP(I32GtS, a > b ? 1 : 0)
    I32_BINOP(I32GtU, static_cast<uint32_t>(a) > static_cast<uint32_t>(b) ? 1 : 0)
    I32_BINOP(I32LeS, a <= b ? 1 : 0)
    I32_BINOP(I32LeU, static_cast<uint32_t>(a) <= static_cast<uint32_t>(b) ? 1 : 0)
    I32_BINOP(I32GeS, a >= b ? 1 : 0)
    I32_BINOP(I32GeU, static_cast<uint32_t>(a) >= static_cast<uint32_t>(b) ? 1 : 0)

    CASE_OP(I32Clz)
        stack_base[sp - 1].i32 = std::countl_zero(static_cast<uint32_t>(stack_base[sp - 1].i32));
        DISPATCH();
    CASE_OP(I32Ctz)
        stack_base[sp - 1].i32 = std::countr_zero(static_cast<uint32_t>(stack_base[sp - 1].i32));
        DISPATCH();
    CASE_OP(I32Popcnt)
        stack_base[sp - 1].i32 = std::popcount(static_cast<uint32_t>(stack_base[sp - 1].i32));
        DISPATCH();

    I32_BINOP(I32Add, static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)))
    I32_BINOP(I32Sub, static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)))
    I32_BINOP(I32Mul, static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)))

    CASE_OP(I32DivS) {
        int32_t b = stack_base[--sp].i32;
        int32_t a = stack_base[sp - 1].i32;
        if (b == 0) throw Trap("integer divide by zero");
        if (a == std::numeric_limits<int32_t>::min() && b == -1) throw Trap("integer overflow");
        stack_base[sp - 1].i32 = a / b;
        DISPATCH();
    }
    CASE_OP(I32DivU) {
        uint32_t b = static_cast<uint32_t>(stack_base[--sp].i32);
        uint32_t a = static_cast<uint32_t>(stack_base[sp - 1].i32);
        if (b == 0) throw Trap("integer divide by zero");
        stack_base[sp - 1].i32 = static_cast<int32_t>(a / b);
        DISPATCH();
    }
    CASE_OP(I32RemS) {
        int32_t b = stack_base[--sp].i32;
        int32_t a = stack_base[sp - 1].i32;
        if (b == 0) throw Trap("integer divide by zero");
        stack_base[sp - 1].i32 = (a == std::numeric_limits<int32_t>::min() && b == -1) ? 0 : (a % b);
        DISPATCH();
    }
    CASE_OP(I32RemU) {
        uint32_t b = static_cast<uint32_t>(stack_base[--sp].i32);
        uint32_t a = static_cast<uint32_t>(stack_base[sp - 1].i32);
        if (b == 0) throw Trap("integer divide by zero");
        stack_base[sp - 1].i32 = static_cast<int32_t>(a % b);
        DISPATCH();
    }

    I32_BINOP(I32And,  a & b)
    I32_BINOP(I32Or,   a | b)
    I32_BINOP(I32Xor,  a ^ b)
    I32_BINOP(I32Shl,  static_cast<int32_t>(static_cast<uint32_t>(a) << (b & 31)))
    I32_BINOP(I32ShrS, a >> (b & 31))
    I32_BINOP(I32ShrU, static_cast<int32_t>(static_cast<uint32_t>(a) >> (b & 31)))
    I32_BINOP(I32Rotl, static_cast<int32_t>(std::rotl(static_cast<uint32_t>(a), b & 31)))
    I32_BINOP(I32Rotr, static_cast<int32_t>(std::rotr(static_cast<uint32_t>(a), b & 31)))

    #undef I32_BINOP

    // --- i64 ops ---

    CASE_OP(I64Eqz)
        stack_base[sp - 1].i32 = (stack_base[sp - 1].i64 == 0) ? 1 : 0;
        DISPATCH();

    #define I64_CMPOP(name, expr) \
        CASE_OP(name) { \
            int64_t b = stack_base[--sp].i64; \
            int64_t a = stack_base[sp - 1].i64; \
            stack_base[sp - 1].i32 = (expr); \
            DISPATCH(); \
        }
    #define I64_BINOP(name, expr) \
        CASE_OP(name) { \
            int64_t b = stack_base[--sp].i64; \
            int64_t a = stack_base[sp - 1].i64; \
            stack_base[sp - 1].i64 = (expr); \
            DISPATCH(); \
        }

    I64_CMPOP(I64Eq,  a == b ? 1 : 0)
    I64_CMPOP(I64Ne,  a != b ? 1 : 0)
    I64_CMPOP(I64LtS, a < b ? 1 : 0)
    I64_CMPOP(I64LtU, static_cast<uint64_t>(a) < static_cast<uint64_t>(b) ? 1 : 0)
    I64_CMPOP(I64GtS, a > b ? 1 : 0)
    I64_CMPOP(I64GtU, static_cast<uint64_t>(a) > static_cast<uint64_t>(b) ? 1 : 0)
    I64_CMPOP(I64LeS, a <= b ? 1 : 0)
    I64_CMPOP(I64LeU, static_cast<uint64_t>(a) <= static_cast<uint64_t>(b) ? 1 : 0)
    I64_CMPOP(I64GeS, a >= b ? 1 : 0)
    I64_CMPOP(I64GeU, static_cast<uint64_t>(a) >= static_cast<uint64_t>(b) ? 1 : 0)

    CASE_OP(I64Clz)
        stack_base[sp - 1].i64 = std::countl_zero(static_cast<uint64_t>(stack_base[sp - 1].i64));
        DISPATCH();
    CASE_OP(I64Ctz)
        stack_base[sp - 1].i64 = std::countr_zero(static_cast<uint64_t>(stack_base[sp - 1].i64));
        DISPATCH();
    CASE_OP(I64Popcnt)
        stack_base[sp - 1].i64 = std::popcount(static_cast<uint64_t>(stack_base[sp - 1].i64));
        DISPATCH();

    I64_BINOP(I64Add, static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b)))
    I64_BINOP(I64Sub, static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b)))
    I64_BINOP(I64Mul, static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b)))

    CASE_OP(I64DivS) {
        int64_t b = stack_base[--sp].i64;
        int64_t a = stack_base[sp - 1].i64;
        if (b == 0) throw Trap("integer divide by zero");
        if (a == std::numeric_limits<int64_t>::min() && b == -1) throw Trap("integer overflow");
        stack_base[sp - 1].i64 = a / b;
        DISPATCH();
    }
    CASE_OP(I64DivU) {
        uint64_t b = static_cast<uint64_t>(stack_base[--sp].i64);
        uint64_t a = static_cast<uint64_t>(stack_base[sp - 1].i64);
        if (b == 0) throw Trap("integer divide by zero");
        stack_base[sp - 1].i64 = static_cast<int64_t>(a / b);
        DISPATCH();
    }
    CASE_OP(I64RemS) {
        int64_t b = stack_base[--sp].i64;
        int64_t a = stack_base[sp - 1].i64;
        if (b == 0) throw Trap("integer divide by zero");
        stack_base[sp - 1].i64 = (a == std::numeric_limits<int64_t>::min() && b == -1) ? 0 : (a % b);
        DISPATCH();
    }
    CASE_OP(I64RemU) {
        uint64_t b = static_cast<uint64_t>(stack_base[--sp].i64);
        uint64_t a = static_cast<uint64_t>(stack_base[sp - 1].i64);
        if (b == 0) throw Trap("integer divide by zero");
        stack_base[sp - 1].i64 = static_cast<int64_t>(a % b);
        DISPATCH();
    }

    I64_BINOP(I64And,  a & b)
    I64_BINOP(I64Or,   a | b)
    I64_BINOP(I64Xor,  a ^ b)
    I64_BINOP(I64Shl,  static_cast<int64_t>(static_cast<uint64_t>(a) << (b & 63)))
    I64_BINOP(I64ShrS, a >> (b & 63))
    I64_BINOP(I64ShrU, static_cast<int64_t>(static_cast<uint64_t>(a) >> (b & 63)))
    I64_BINOP(I64Rotl, static_cast<int64_t>(std::rotl(static_cast<uint64_t>(a), static_cast<int>(b & 63))))
    I64_BINOP(I64Rotr, static_cast<int64_t>(std::rotr(static_cast<uint64_t>(a), static_cast<int>(b & 63))))

    #undef I64_CMPOP
    #undef I64_BINOP

    // --- f32 ops ---

    #define F32_CMPOP(name, expr) \
        CASE_OP(name) { \
            float b = stack_base[--sp].f32; \
            float a = stack_base[sp - 1].f32; \
            stack_base[sp - 1].i32 = (expr) ? 1 : 0; \
            DISPATCH(); \
        }
    #define F32_UNOP(name, expr) \
        CASE_OP(name) { \
            float a = stack_base[sp - 1].f32; \
            stack_base[sp - 1].f32 = (expr); \
            DISPATCH(); \
        }
    #define F32_BINOP(name, expr) \
        CASE_OP(name) { \
            float b = stack_base[--sp].f32; \
            float a = stack_base[sp - 1].f32; \
            stack_base[sp - 1].f32 = (expr); \
            DISPATCH(); \
        }

    F32_CMPOP(F32Eq, a == b)
    F32_CMPOP(F32Ne, a != b)
    F32_CMPOP(F32Lt, a < b)
    F32_CMPOP(F32Gt, a > b)
    F32_CMPOP(F32Le, a <= b)
    F32_CMPOP(F32Ge, a >= b)

    F32_UNOP(F32Abs,     std::fabs(a))
    F32_UNOP(F32Neg,     -a)
    F32_UNOP(F32Ceil,    std::ceil(a))
    F32_UNOP(F32Floor,   std::floor(a))
    F32_UNOP(F32Trunc,   std::trunc(a))
    F32_UNOP(F32Nearest, wasm_nearest(a))
    F32_UNOP(F32Sqrt,    std::sqrt(a))

    F32_BINOP(F32Add,      a + b)
    F32_BINOP(F32Sub,      a - b)
    F32_BINOP(F32Mul,      a * b)
    F32_BINOP(F32Div,      a / b)
    F32_BINOP(F32Min,      wasm_min(a, b))
    F32_BINOP(F32Max,      wasm_max(a, b))
    F32_BINOP(F32Copysign, std::copysign(a, b))

    #undef F32_CMPOP
    #undef F32_UNOP
    #undef F32_BINOP

    // --- f64 ops ---

    #define F64_CMPOP(name, expr) \
        CASE_OP(name) { \
            double b = stack_base[--sp].f64; \
            double a = stack_base[sp - 1].f64; \
            stack_base[sp - 1].i32 = (expr) ? 1 : 0; \
            DISPATCH(); \
        }
    #define F64_UNOP(name, expr) \
        CASE_OP(name) { \
            double a = stack_base[sp - 1].f64; \
            stack_base[sp - 1].f64 = (expr); \
            DISPATCH(); \
        }
    #define F64_BINOP(name, expr) \
        CASE_OP(name) { \
            double b = stack_base[--sp].f64; \
            double a = stack_base[sp - 1].f64; \
            stack_base[sp - 1].f64 = (expr); \
            DISPATCH(); \
        }

    F64_CMPOP(F64Eq, a == b)
    F64_CMPOP(F64Ne, a != b)
    F64_CMPOP(F64Lt, a < b)
    F64_CMPOP(F64Gt, a > b)
    F64_CMPOP(F64Le, a <= b)
    F64_CMPOP(F64Ge, a >= b)

    F64_UNOP(F64Abs,     std::fabs(a))
    F64_UNOP(F64Neg,     -a)
    F64_UNOP(F64Ceil,    std::ceil(a))
    F64_UNOP(F64Floor,   std::floor(a))
    F64_UNOP(F64Trunc,   std::trunc(a))
    F64_UNOP(F64Nearest, wasm_nearest(a))
    F64_UNOP(F64Sqrt,    std::sqrt(a))

    F64_BINOP(F64Add,      a + b)
    F64_BINOP(F64Sub,      a - b)
    F64_BINOP(F64Mul,      a * b)
    F64_BINOP(F64Div,      a / b)
    F64_BINOP(F64Min,      wasm_min(a, b))
    F64_BINOP(F64Max,      wasm_max(a, b))
    F64_BINOP(F64Copysign, std::copysign(a, b))

    #undef F64_CMPOP
    #undef F64_UNOP
    #undef F64_BINOP

    // --- Conversions & Reinterpretations ---

    CASE_OP(I32WrapI64)
        stack_base[sp - 1].i32 = static_cast<int32_t>(stack_base[sp - 1].i64);
        DISPATCH();
    CASE_OP(I64ExtendI32S)
        stack_base[sp - 1].i64 = static_cast<int64_t>(stack_base[sp - 1].i32);
        DISPATCH();
    CASE_OP(I64ExtendI32U)
        stack_base[sp - 1].i64 = static_cast<int64_t>(static_cast<uint32_t>(stack_base[sp - 1].i32));
        DISPATCH();
    CASE_OP(I32TruncF32S)
        stack_base[sp - 1].i32 = trunc_f32_s(stack_base[sp - 1].f32);
        DISPATCH();
    CASE_OP(I32TruncF32U)
        stack_base[sp - 1].i32 = trunc_f32_u(stack_base[sp - 1].f32);
        DISPATCH();
    CASE_OP(I32TruncF64S)
        stack_base[sp - 1].i32 = trunc_f64_s(stack_base[sp - 1].f64);
        DISPATCH();
    CASE_OP(I32TruncF64U)
        stack_base[sp - 1].i32 = trunc_f64_u(stack_base[sp - 1].f64);
        DISPATCH();
    CASE_OP(I64TruncF32S)
        stack_base[sp - 1].i64 = trunc_f32_i64_s(stack_base[sp - 1].f32);
        DISPATCH();
    CASE_OP(I64TruncF32U)
        stack_base[sp - 1].i64 = trunc_f32_i64_u(stack_base[sp - 1].f32);
        DISPATCH();
    CASE_OP(I64TruncF64S)
        stack_base[sp - 1].i64 = trunc_f64_i64_s(stack_base[sp - 1].f64);
        DISPATCH();
    CASE_OP(I64TruncF64U)
        stack_base[sp - 1].i64 = trunc_f64_i64_u(stack_base[sp - 1].f64);
        DISPATCH();
    CASE_OP(F32ConvertI32S)
        stack_base[sp - 1].f32 = static_cast<float>(stack_base[sp - 1].i32);
        DISPATCH();
    CASE_OP(F32ConvertI32U)
        stack_base[sp - 1].f32 = static_cast<float>(static_cast<uint32_t>(stack_base[sp - 1].i32));
        DISPATCH();
    CASE_OP(F32ConvertI64S)
        stack_base[sp - 1].f32 = static_cast<float>(stack_base[sp - 1].i64);
        DISPATCH();
    CASE_OP(F32ConvertI64U)
        stack_base[sp - 1].f32 = static_cast<float>(static_cast<uint64_t>(stack_base[sp - 1].i64));
        DISPATCH();
    CASE_OP(F64ConvertI32S)
        stack_base[sp - 1].f64 = static_cast<double>(stack_base[sp - 1].i32);
        DISPATCH();
    CASE_OP(F64ConvertI32U)
        stack_base[sp - 1].f64 = static_cast<double>(static_cast<uint32_t>(stack_base[sp - 1].i32));
        DISPATCH();
    CASE_OP(F64ConvertI64S)
        stack_base[sp - 1].f64 = static_cast<double>(stack_base[sp - 1].i64);
        DISPATCH();
    CASE_OP(F64ConvertI64U)
        stack_base[sp - 1].f64 = static_cast<double>(static_cast<uint64_t>(stack_base[sp - 1].i64));
        DISPATCH();
    CASE_OP(F32DemoteF64)
        stack_base[sp - 1].f32 = static_cast<float>(stack_base[sp - 1].f64);
        DISPATCH();
    CASE_OP(F64PromoteF32)
        stack_base[sp - 1].f64 = static_cast<double>(stack_base[sp - 1].f32);
        DISPATCH();
    CASE_OP(I32ReinterpretF32)
        stack_base[sp - 1].i32 = std::bit_cast<int32_t>(stack_base[sp - 1].f32);
        DISPATCH();
    CASE_OP(I64ReinterpretF64)
        stack_base[sp - 1].i64 = std::bit_cast<int64_t>(stack_base[sp - 1].f64);
        DISPATCH();
    CASE_OP(F32ReinterpretI32)
        stack_base[sp - 1].f32 = std::bit_cast<float>(stack_base[sp - 1].i32);
        DISPATCH();
    CASE_OP(F64ReinterpretI64)
        stack_base[sp - 1].f64 = std::bit_cast<double>(stack_base[sp - 1].i64);
        DISPATCH();
    CASE_OP(I32Extend8S)
        stack_base[sp - 1].i32 = static_cast<int32_t>(static_cast<int8_t>(stack_base[sp - 1].i32));
        DISPATCH();
    CASE_OP(I32Extend16S)
        stack_base[sp - 1].i32 = static_cast<int32_t>(static_cast<int16_t>(stack_base[sp - 1].i32));
        DISPATCH();
    CASE_OP(I64Extend8S)
        stack_base[sp - 1].i64 = static_cast<int64_t>(static_cast<int8_t>(stack_base[sp - 1].i64));
        DISPATCH();
    CASE_OP(I64Extend16S)
        stack_base[sp - 1].i64 = static_cast<int64_t>(static_cast<int16_t>(stack_base[sp - 1].i64));
        DISPATCH();
    CASE_OP(I64Extend32S)
        stack_base[sp - 1].i64 = static_cast<int64_t>(static_cast<int32_t>(stack_base[sp - 1].i64));
        DISPATCH();

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#else
        case OpCode::Count:
            throw Trap("invalid opcode");
        }
    }
#endif
    #undef DISPATCH
    #undef CASE_OP
}

} // namespace optrich
