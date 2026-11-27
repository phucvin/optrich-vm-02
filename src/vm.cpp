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
    if (!module_.funcImports.empty() || !module_.tableImports.empty()) {
        throw LinkError("module has unresolved imports; instantiate via Runtime");
    }
    init_storage_and_elems();
}

ModuleInstance::ModuleInstance(CompiledModule module, MemoryStore& store,
                               std::vector<FuncRef> imported_funcs,
                               std::vector<std::shared_ptr<FuncTable>> imported_tables)
    : module_(std::move(module)),
      store_(store),
      funcs_(std::move(imported_funcs)),
      tables_(std::move(imported_tables)) {
    init_storage_and_elems();
}

void ModuleInstance::init_storage_and_elems() {
    // Append local functions to unified function table
    funcs_.reserve(module_.total_funcs());
    for (uint32_t i = 0; i < module_.funcs.size(); ++i) {
        funcs_.push_back(FuncRef{this, i, nullptr});
    }

    // Append local tables to unified table list
    tables_.reserve(module_.total_tables());
    for (const auto& td : module_.tables) {
        auto tbl = std::make_shared<FuncTable>();
        tbl->elements.resize(td.initialSize);
        tables_.push_back(std::move(tbl));
    }

    // Populate element segments (works across imported/shared tables!)
    for (const auto& seg : module_.elems) {
        if (seg.tableIdx >= tables_.size()) {
            throw Trap("elem segment table index out of range");
        }
        FuncTable& tbl = *tables_[seg.tableIdx];
        if (static_cast<uint64_t>(seg.offset) + seg.funcIndices.size() > tbl.elements.size()) {
            throw Trap("elem segment out of table bounds");
        }
        for (size_t i = 0; i < seg.funcIndices.size(); ++i) {
            tbl.elements[seg.offset + i] = funcs_[seg.funcIndices[i]];
        }
    }

    // Pre-allocate string constants as read-only handles in the shared MemoryStore.
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

FuncRef ModuleInstance::find_export_func(std::string_view name) const {
    return funcs_[find_export(name)];
}

std::shared_ptr<FuncTable> ModuleInstance::find_export_table(std::string_view name) const {
    auto it = module_.tableExports.find(std::string(name));
    if (it == module_.tableExports.end()) {
        throw LinkError("table export not found: '" + std::string(name) + "'");
    }
    return tables_[it->second];
}

void ModuleInstance::collect_roots(std::vector<HandleId>& out) const {
    for (const auto& fh : stringHandles_) {
        if (!fh.is_null()) out.push_back(fh.id);
    }
    for (const auto& g : globals_) {
        if (g.is_handle() && !g.handle.is_null()) {
            out.push_back(g.handle.id);
        }
    }
}

VM::VM(size_t max_stack_values, size_t max_call_depth)
    : maxStack_(max_stack_values), maxCallDepth_(max_call_depth) {
    stack_.resize(maxStack_);
    frames_.resize(maxCallDepth_);
}

void VM::collect_roots(std::vector<HandleId>& out) const {
    for (size_t i = 0; i < activeSp_; ++i) {
        if (stack_[i].is_handle() && !stack_[i].handle.is_null()) {
            out.push_back(stack_[i].handle.id);
        }
    }
}

std::vector<Value> VM::invoke(ModuleInstance& inst, std::string_view export_name,
                              const std::vector<Value>& args) {
    uint32_t fidx = inst.find_export(export_name);
    return invoke_index(inst, fidx, args);
}

std::vector<Value> VM::invoke_index(ModuleInstance& inst, uint32_t func_idx,
                                    const std::vector<Value>& args) {
    if (func_idx >= inst.module().total_funcs()) {
        throw Trap("function index out of range");
    }
    const FuncRef& entry_ref = inst.func_ref(func_idx);
    if (entry_ref.is_host()) {
        return entry_ref.host->fn(inst.store(), args);
    }

    ModuleInstance* cur_inst = entry_ref.instance;
    const CompiledFunc* func = &cur_inst->module().funcs[entry_ref.localFuncIdx];
    if (args.size() != func->paramCount) {
        throw Trap("argument count mismatch: expected " + std::to_string(func->paramCount) +
                   ", got " + std::to_string(args.size()));
    }

    Value* const stack_base = stack_.data();
    size_t sp = 0;

    // Ensure activeSp_ is reset even if a Trap is thrown.
    struct ActiveSpGuard {
        size_t& ref;
        ~ActiveSpGuard() { ref = 0; }
    } sp_guard{activeSp_};

    if (func->locals.size() >= maxStack_) {
        throw Trap("stack overflow");
    }

    for (size_t i = 0; i < args.size(); ++i) {
        stack_base[sp++] = args[i];
    }
    for (size_t i = func->paramCount; i < func->locals.size(); ++i) {
        stack_base[sp++] = Value::zero(func->locals[i]);
    }
    activeSp_ = sp;

    size_t locals_base = 0;
    Value* locals = stack_base;
    const Instr* code_base = func->code.data();
    const Instr* pc = code_base;
    CallFrame* fp = frames_.data();
    CallFrame* const fp_end = fp + maxCallDepth_;
    *fp = CallFrame{cur_inst, func, pc, locals_base, sp};

    MemoryStore& store = cur_inst->store();
    Value* globals = cur_inst->globals().data();
    const FatHandle* str_handles = cur_inst->string_handles().data();
    const CompiledFunc* funcs_base = cur_inst->module().funcs.data();

    auto set_i32 = [&](size_t idx, int32_t v) {
        stack_base[idx].type = ValType::I32;
        stack_base[idx].i32 = v;
    };
    auto set_i64 = [&](size_t idx, int64_t v) {
        stack_base[idx].type = ValType::I64;
        stack_base[idx].i64 = v;
    };
    auto set_f32 = [&](size_t idx, float v) {
        stack_base[idx].type = ValType::F32;
        stack_base[idx].f32 = v;
    };
    auto set_f64 = [&](size_t idx, double v) {
        stack_base[idx].type = ValType::F64;
        stack_base[idx].f64 = v;
    };
    auto set_handle = [&](size_t idx, FatHandle h) {
        stack_base[idx].type = ValType::Handle;
        stack_base[idx].handle = h;
    };
    auto set_funcref = [&](size_t idx, FuncRef f) {
        stack_base[idx].type = ValType::FuncRef;
        stack_base[idx].funcref = f;
    };

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
        pc = code_base + target_pc;
    };

    auto dispatch_call = [&](const FuncRef& target) {
        if (target.is_host()) {
            const HostFuncEntry* host = target.host;
            size_t nparams = host->sig.params.size();
            std::vector<Value> hargs(nparams);
            for (size_t k = 0; k < nparams; ++k) {
                hargs[k] = stack_base[sp - nparams + k];
            }
            // Keep args rooted on stack while host executes in case it triggers GC!
            activeSp_ = sp;
            std::vector<Value> hres = host->fn(store, hargs);
            sp -= nparams;
            for (const auto& r : hres) {
                stack_base[sp++] = r;
            }
            activeSp_ = sp;
            return;
        }

        if (fp + 1 >= fp_end) {
            throw Trap("call stack overflow");
        }
        fp->pc = pc;

        ModuleInstance* next_inst = target.instance;
        const CompiledFunc* callee = &next_inst->module().funcs[target.localFuncIdx];
        size_t new_locals_base = sp - callee->paramCount;
        uint32_t extra_locals = callee->extraLocals;
        if (sp + extra_locals + 64 >= maxStack_) {
            throw Trap("operand stack overflow");
        }
        for (size_t k = callee->paramCount; k < callee->locals.size(); ++k) {
            stack_base[sp++] = Value::zero(callee->locals[k]);
        }

        if (next_inst != cur_inst) {
            cur_inst = next_inst;
            globals = cur_inst->globals().data();
            str_handles = cur_inst->string_handles().data();
            funcs_base = cur_inst->module().funcs.data();
        }
        func = callee;
        code_base = callee->code.data();
        pc = code_base;
        locals_base = new_locals_base;
        locals = stack_base + locals_base;
        ++fp;
        *fp = CallFrame{cur_inst, func, pc, locals_base, sp};
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
        &&L_CallLocal,
        &&L_CallIndirect,
        &&L_CallRef,
        &&L_Drop,
        &&L_Select,
        &&L_LocalGet,
        &&L_LocalGetPrim,
        &&L_LocalSet,
        &&L_LocalSetPrim,
        &&L_LocalTee,
        &&L_LocalTeePrim,
        &&L_GlobalGet,
        &&L_GlobalSet,
        &&L_I32Const,
        &&L_I64Const,
        &&L_F32Const,
        &&L_F64Const,
        &&L_HandleNull,
        &&L_MemConstStr,
        &&L_RefFunc,
        &&L_RefNullFunc,
        &&L_RefIsNull,
        &&L_MemAlloc,
        &&L_MemAllocVtable,
        &&L_MemSpan,
        &&L_MemRefSpan,
        &&L_MemReadOnly,
        &&L_MemCloneSpan,
        &&L_MemByteLen,
        &&L_MemRefLen,
        &&L_MemFuncLen,
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
        &&L_MemReadFunc,
        &&L_MemWriteFunc,
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
        &&L_LocalIncI32, &&L_LocalGetAddI32C, &&L_LocalGetSubI32C,
        &&L_BrIfLocalGeI32C, &&L_BrIfLocalGeS, &&L_BrIfLocalGeU,
        &&L_LocalMemReadI32, &&L_LocalMemReadU8,
        &&L_I32AddC, &&L_I32SubC, &&L_I32MulC, &&L_I32ShrUC,
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
        uint32_t res_count = func->resultCount;
        size_t ret_dst = locals_base;
        if (res_count == 1) {
            const Value& rv = stack_base[sp - 1];
            if (static_cast<uint8_t>(rv.type) < static_cast<uint8_t>(ValType::Handle)) {
                stack_base[ret_dst].type = rv.type;
                stack_base[ret_dst].i64 = rv.i64;
            } else {
                stack_base[ret_dst] = rv;
            }
        } else if (res_count > 1) {
            size_t res_src = sp - res_count;
            for (uint32_t k = 0; k < res_count; ++k) {
                stack_base[ret_dst + k] = stack_base[res_src + k];
            }
        }
        sp = ret_dst + res_count;
        if (fp == frames_.data()) {
            std::vector<Value> results(res_count);
            for (uint32_t k = 0; k < res_count; ++k) {
                results[k] = stack_base[k];
            }
            return results;
        }
        --fp;
        ModuleInstance* prev_inst = fp->inst;
        func = fp->func;
        code_base = func->code.data();
        pc = fp->pc;
        locals_base = fp->localsBase;
        locals = stack_base + locals_base;
        if (prev_inst != cur_inst) {
            cur_inst = prev_inst;
            globals = cur_inst->globals().data();
            str_handles = cur_inst->string_handles().data();
            funcs_base = cur_inst->module().funcs.data();
        }
        DISPATCH();
    }

    CASE_OP(Call) {
        dispatch_call(cur_inst->func_ref(ins->imm.u32));
        DISPATCH();
    }

    CASE_OP(CallLocal) {
        if (fp + 1 >= fp_end) {
            throw Trap("call stack overflow");
        }
        fp->pc = pc;
        const CompiledFunc* callee = funcs_base + ins->imm.u32;
        size_t new_locals_base = sp - callee->paramCount;
        uint32_t extra = callee->extraLocals;
        if (extra != 0) {
            if (sp + extra + 64 >= maxStack_) {
                throw Trap("operand stack overflow");
            }
            for (size_t k = callee->paramCount; k < callee->locals.size(); ++k) {
                stack_base[sp++] = Value::zero(callee->locals[k]);
            }
        } else if (sp + 64 >= maxStack_) {
            throw Trap("operand stack overflow");
        }
        func = callee;
        code_base = callee->code.data();
        pc = code_base;
        locals_base = new_locals_base;
        locals = stack_base + locals_base;
        ++fp;
        *fp = CallFrame{cur_inst, func, pc, locals_base, sp};
        DISPATCH();
    }

    CASE_OP(CallIndirect) {
        uint32_t elem_idx = static_cast<uint32_t>(stack_base[--sp].i32);
        const FuncTable& tbl = cur_inst->table(ins->aux);
        if (elem_idx >= tbl.elements.size()) {
            throw Trap("call_indirect table index out of bounds");
        }
        const FuncRef& target = tbl.elements[elem_idx];
        if (target.is_null()) {
            throw Trap("call_indirect uninitialized table element");
        }
        const FuncSignature& expected_sig = cur_inst->module().types[ins->imm.u32];
        const FuncSignature& actual_sig =
            target.is_host() ? target.host->sig
                             : target.instance->module().funcs[target.localFuncIdx].sig;
        if (actual_sig != expected_sig) {
            throw Trap("call_indirect signature mismatch");
        }
        dispatch_call(target);
        DISPATCH();
    }

    CASE_OP(CallRef) {
        FuncRef target = stack_base[--sp].funcref;
        if (target.is_null()) {
            throw Trap("call_ref on null funcref");
        }
        const FuncSignature& expected_sig = cur_inst->module().types[ins->imm.u32];
        const FuncSignature& actual_sig =
            target.is_host() ? target.host->sig
                             : target.instance->module().funcs[target.localFuncIdx].sig;
        if (actual_sig != expected_sig) {
            throw Trap("call_ref signature mismatch");
        }
        dispatch_call(target);
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

    CASE_OP(LocalGetPrim) {
        const Value& src = locals[ins->imm.u32];
        stack_base[sp].type = src.type;
        stack_base[sp].i64 = src.i64;
        ++sp;
        DISPATCH();
    }

    CASE_OP(LocalSet)
        locals[ins->imm.u32] = stack_base[--sp];
        DISPATCH();

    CASE_OP(LocalSetPrim)
        locals[ins->imm.u32].i64 = stack_base[--sp].i64;
        DISPATCH();

    CASE_OP(LocalTee)
        locals[ins->imm.u32] = stack_base[sp - 1];
        DISPATCH();

    CASE_OP(LocalTeePrim)
        locals[ins->imm.u32].i64 = stack_base[sp - 1].i64;
        DISPATCH();

    CASE_OP(GlobalGet)
        stack_base[sp++] = globals[ins->imm.u32];
        DISPATCH();

    CASE_OP(GlobalSet)
        globals[ins->imm.u32] = stack_base[--sp];
        DISPATCH();

    CASE_OP(I32Const)
        set_i32(sp++, ins->imm.i32);
        DISPATCH();

    CASE_OP(I64Const)
        set_i64(sp++, ins->imm.i64);
        DISPATCH();

    CASE_OP(F32Const)
        set_f32(sp++, ins->imm.f32);
        DISPATCH();

    CASE_OP(F64Const)
        set_f64(sp++, ins->imm.f64);
        DISPATCH();

    CASE_OP(HandleNull)
        set_handle(sp++, FatHandle::null());
        DISPATCH();

    CASE_OP(MemConstStr)
        set_handle(sp++, str_handles[ins->imm.u32]);
        DISPATCH();

    CASE_OP(RefFunc)
        set_funcref(sp++, cur_inst->func_ref(ins->imm.u32));
        DISPATCH();

    CASE_OP(RefNullFunc)
        set_funcref(sp++, FuncRef::null());
        DISPATCH();

    CASE_OP(RefIsNull) {
        FuncRef f = stack_base[sp - 1].funcref;
        set_i32(sp - 1, f.is_null() ? 1 : 0);
        DISPATCH();
    }

    // --- Native Split-Block & Vtable Memory Opcodes (GC-safepointed!) ---

    CASE_OP(MemAlloc) {
        int32_t num_refs  = stack_base[sp - 1].i32;
        int32_t num_bytes = stack_base[sp - 2].i32;
        if (num_bytes < 0 || num_refs < 0) {
            throw Trap("mem.alloc negative size");
        }
        sp -= 2;
        activeSp_ = sp; // GC safepoint: all live handles below sp are traced
        FatHandle res = store.alloc(static_cast<uint32_t>(num_bytes),
                                    static_cast<uint32_t>(num_refs));
        set_handle(sp++, res);
        DISPATCH();
    }

    CASE_OP(MemAllocVtable) {
        int32_t num_funcs = stack_base[sp - 1].i32;
        if (num_funcs < 0) {
            throw Trap("mem.alloc_vtable negative size");
        }
        --sp;
        activeSp_ = sp;
        FatHandle res = store.alloc_vtable(static_cast<uint32_t>(num_funcs));
        set_handle(sp++, res);
        DISPATCH();
    }

    CASE_OP(MemSpan) {
        int32_t byte_len = stack_base[sp - 1].i32;
        int32_t byte_off = stack_base[sp - 2].i32;
        FatHandle h      = stack_base[sp - 3].handle;
        if (byte_off < 0 || byte_len < 0) {
            throw Trap("mem.span negative offset or length");
        }
        // Keep `h` rooted at stack_base[sp - 3] during store.make_span in case GC runs!
        activeSp_ = sp - 2;
        FatHandle res = store.make_span(h.id, static_cast<uint32_t>(byte_off),
                                        static_cast<uint32_t>(byte_len));
        sp -= 2;
        set_handle(sp - 1, res);
        DISPATCH();
    }

    CASE_OP(MemRefSpan) {
        int32_t ref_len = stack_base[sp - 1].i32;
        int32_t ref_off = stack_base[sp - 2].i32;
        FatHandle h     = stack_base[sp - 3].handle;
        if (ref_off < 0 || ref_len < 0) {
            throw Trap("mem.ref_span negative offset or length");
        }
        activeSp_ = sp - 2;
        FatHandle res = store.make_ref_span(h.id, static_cast<uint32_t>(ref_off),
                                            static_cast<uint32_t>(ref_len));
        sp -= 2;
        set_handle(sp - 1, res);
        DISPATCH();
    }

    CASE_OP(MemReadOnly) {
        FatHandle h = stack_base[sp - 1].handle;
        activeSp_ = sp; // `h` remains rooted at sp - 1 during GC
        FatHandle res = store.as_readonly(h.id);
        set_handle(sp - 1, res);
        DISPATCH();
    }

    CASE_OP(MemCloneSpan) {
        FatHandle h = stack_base[sp - 1].handle;
        activeSp_ = sp; // `h` remains rooted at sp - 1 during GC
        FatHandle res = store.clone_span(h.id);
        set_handle(sp - 1, res);
        DISPATCH();
    }

    CASE_OP(MemByteLen) {
        FatHandle h = stack_base[sp - 1].handle;
        if ((h.flags & kFlagValid) == 0) {
            throw Trap("mem.byte_len on null handle");
        }
        set_i32(sp - 1, static_cast<int32_t>(h.byteSize));
        DISPATCH();
    }

    CASE_OP(MemRefLen) {
        FatHandle h = stack_base[sp - 1].handle;
        set_i32(sp - 1, static_cast<int32_t>(store.ref_len(h.id)));
        DISPATCH();
    }

    CASE_OP(MemFuncLen) {
        FatHandle h = stack_base[sp - 1].handle;
        set_i32(sp - 1, static_cast<int32_t>(store.func_len(h.id)));
        DISPATCH();
    }

    CASE_OP(MemIsReadOnly) {
        FatHandle h = stack_base[sp - 1].handle;
        if ((h.flags & kFlagValid) == 0) {
            throw Trap("mem.is_readonly on null handle");
        }
        set_i32(sp - 1, ((h.flags & kFlagReadOnly) != 0) ? 1 : 0);
        DISPATCH();
    }

    CASE_OP(MemIsNull) {
        FatHandle h = stack_base[sp - 1].handle;
        set_i32(sp - 1, h.is_null() ? 1 : 0);
        DISPATCH();
    }

    CASE_OP(MemEq) {
        FatHandle b = stack_base[--sp].handle;
        FatHandle a = stack_base[sp - 1].handle;
        set_i32(sp - 1, (a.id == b.id) ? 1 : 0);
        DISPATCH();
    }

    // --- Fast-path Typed Byte Reads (zero handle-table lookup) ---

    CASE_OP(MemReadU8) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        set_i32(sp - 1, static_cast<int32_t>(fat_read<uint8_t>(h, off)));
        DISPATCH();
    }

    CASE_OP(MemReadI8) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        set_i32(sp - 1, static_cast<int32_t>(fat_read<int8_t>(h, off)));
        DISPATCH();
    }

    CASE_OP(MemReadU16) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        set_i32(sp - 1, static_cast<int32_t>(fat_read<uint16_t>(h, off)));
        DISPATCH();
    }

    CASE_OP(MemReadI16) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        set_i32(sp - 1, static_cast<int32_t>(fat_read<int16_t>(h, off)));
        DISPATCH();
    }

    CASE_OP(MemReadI32) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        set_i32(sp - 1, fat_read<int32_t>(h, off));
        DISPATCH();
    }

    CASE_OP(MemReadI64) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        set_i64(sp - 1, fat_read<int64_t>(h, off));
        DISPATCH();
    }

    CASE_OP(MemReadF32) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        set_f32(sp - 1, fat_read<float>(h, off));
        DISPATCH();
    }

    CASE_OP(MemReadF64) {
        uint32_t off = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h  = stack_base[sp - 1].handle;
        set_f64(sp - 1, fat_read<double>(h, off));
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
        set_handle(sp - 1, store.read_ref(h.id, slot));
        DISPATCH();
    }

    CASE_OP(MemWriteRef) {
        FatHandle child = stack_base[--sp].handle;
        uint32_t slot   = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h     = stack_base[--sp].handle;
        store.write_ref(h.id, slot, child.id);
        DISPATCH();
    }

    CASE_OP(MemReadFunc) {
        uint32_t slot = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h   = stack_base[sp - 1].handle;
        set_funcref(sp - 1, store.read_func(h.id, slot));
        DISPATCH();
    }

    CASE_OP(MemWriteFunc) {
        FuncRef fn    = stack_base[--sp].funcref;
        uint32_t slot = static_cast<uint32_t>(stack_base[--sp].i32);
        FatHandle h   = stack_base[--sp].handle;
        store.write_func(h.id, slot, fn);
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
        set_i32(sp - 1, (cmp < 0) ? -1 : ((cmp > 0) ? 1 : 0));
        DISPATCH();
    }

    // --- i32 ops ---

    CASE_OP(I32Eqz)
        set_i32(sp - 1, (stack_base[sp - 1].i32 == 0) ? 1 : 0);
        DISPATCH();

    #define I32_BINOP(name, expr) \
        CASE_OP(name) { \
            int32_t b = stack_base[--sp].i32; \
            int32_t a = stack_base[sp - 1].i32; \
            set_i32(sp - 1, (expr)); \
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
        set_i32(sp - 1, std::countl_zero(static_cast<uint32_t>(stack_base[sp - 1].i32)));
        DISPATCH();
    CASE_OP(I32Ctz)
        set_i32(sp - 1, std::countr_zero(static_cast<uint32_t>(stack_base[sp - 1].i32)));
        DISPATCH();
    CASE_OP(I32Popcnt)
        set_i32(sp - 1, std::popcount(static_cast<uint32_t>(stack_base[sp - 1].i32)));
        DISPATCH();

    I32_BINOP(I32Add, static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)))
    I32_BINOP(I32Sub, static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)))
    I32_BINOP(I32Mul, static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)))

    CASE_OP(I32DivS) {
        int32_t b = stack_base[--sp].i32;
        int32_t a = stack_base[sp - 1].i32;
        if (b == 0) throw Trap("integer divide by zero");
        if (a == std::numeric_limits<int32_t>::min() && b == -1) throw Trap("integer overflow");
        set_i32(sp - 1, a / b);
        DISPATCH();
    }
    CASE_OP(I32DivU) {
        uint32_t b = static_cast<uint32_t>(stack_base[--sp].i32);
        uint32_t a = static_cast<uint32_t>(stack_base[sp - 1].i32);
        if (b == 0) throw Trap("integer divide by zero");
        set_i32(sp - 1, static_cast<int32_t>(a / b));
        DISPATCH();
    }
    CASE_OP(I32RemS) {
        int32_t b = stack_base[--sp].i32;
        int32_t a = stack_base[sp - 1].i32;
        if (b == 0) throw Trap("integer divide by zero");
        set_i32(sp - 1, (a == std::numeric_limits<int32_t>::min() && b == -1) ? 0 : (a % b));
        DISPATCH();
    }
    CASE_OP(I32RemU) {
        uint32_t b = static_cast<uint32_t>(stack_base[--sp].i32);
        uint32_t a = static_cast<uint32_t>(stack_base[sp - 1].i32);
        if (b == 0) throw Trap("integer divide by zero");
        set_i32(sp - 1, static_cast<int32_t>(a % b));
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
        set_i32(sp - 1, (stack_base[sp - 1].i64 == 0) ? 1 : 0);
        DISPATCH();

    #define I64_CMPOP(name, expr) \
        CASE_OP(name) { \
            int64_t b = stack_base[--sp].i64; \
            int64_t a = stack_base[sp - 1].i64; \
            set_i32(sp - 1, (expr)); \
            DISPATCH(); \
        }
    #define I64_BINOP(name, expr) \
        CASE_OP(name) { \
            int64_t b = stack_base[--sp].i64; \
            int64_t a = stack_base[sp - 1].i64; \
            set_i64(sp - 1, (expr)); \
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
        set_i64(sp - 1, std::countl_zero(static_cast<uint64_t>(stack_base[sp - 1].i64)));
        DISPATCH();
    CASE_OP(I64Ctz)
        set_i64(sp - 1, std::countr_zero(static_cast<uint64_t>(stack_base[sp - 1].i64)));
        DISPATCH();
    CASE_OP(I64Popcnt)
        set_i64(sp - 1, std::popcount(static_cast<uint64_t>(stack_base[sp - 1].i64)));
        DISPATCH();

    I64_BINOP(I64Add, static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b)))
    I64_BINOP(I64Sub, static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b)))
    I64_BINOP(I64Mul, static_cast<int64_t>(static_cast<uint64_t>(a) * static_cast<uint64_t>(b)))

    CASE_OP(I64DivS) {
        int64_t b = stack_base[--sp].i64;
        int64_t a = stack_base[sp - 1].i64;
        if (b == 0) throw Trap("integer divide by zero");
        if (a == std::numeric_limits<int64_t>::min() && b == -1) throw Trap("integer overflow");
        set_i64(sp - 1, a / b);
        DISPATCH();
    }
    CASE_OP(I64DivU) {
        uint64_t b = static_cast<uint64_t>(stack_base[--sp].i64);
        uint64_t a = static_cast<uint64_t>(stack_base[sp - 1].i64);
        if (b == 0) throw Trap("integer divide by zero");
        set_i64(sp - 1, static_cast<int64_t>(a / b));
        DISPATCH();
    }
    CASE_OP(I64RemS) {
        int64_t b = stack_base[--sp].i64;
        int64_t a = stack_base[sp - 1].i64;
        if (b == 0) throw Trap("integer divide by zero");
        set_i64(sp - 1, (a == std::numeric_limits<int64_t>::min() && b == -1) ? 0 : (a % b));
        DISPATCH();
    }
    CASE_OP(I64RemU) {
        uint64_t b = static_cast<uint64_t>(stack_base[--sp].i64);
        uint64_t a = static_cast<uint64_t>(stack_base[sp - 1].i64);
        if (b == 0) throw Trap("integer divide by zero");
        set_i64(sp - 1, static_cast<int64_t>(a % b));
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
            set_i32(sp - 1, (expr) ? 1 : 0); \
            DISPATCH(); \
        }
    #define F32_UNOP(name, expr) \
        CASE_OP(name) { \
            float a = stack_base[sp - 1].f32; \
            set_f32(sp - 1, (expr)); \
            DISPATCH(); \
        }
    #define F32_BINOP(name, expr) \
        CASE_OP(name) { \
            float b = stack_base[--sp].f32; \
            float a = stack_base[sp - 1].f32; \
            set_f32(sp - 1, (expr)); \
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
            set_i32(sp - 1, (expr) ? 1 : 0); \
            DISPATCH(); \
        }
    #define F64_UNOP(name, expr) \
        CASE_OP(name) { \
            double a = stack_base[sp - 1].f64; \
            set_f64(sp - 1, (expr)); \
            DISPATCH(); \
        }
    #define F64_BINOP(name, expr) \
        CASE_OP(name) { \
            double b = stack_base[--sp].f64; \
            double a = stack_base[sp - 1].f64; \
            set_f64(sp - 1, (expr)); \
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
        set_i32(sp - 1, static_cast<int32_t>(stack_base[sp - 1].i64));
        DISPATCH();
    CASE_OP(I64ExtendI32S)
        set_i64(sp - 1, static_cast<int64_t>(stack_base[sp - 1].i32));
        DISPATCH();
    CASE_OP(I64ExtendI32U)
        set_i64(sp - 1, static_cast<int64_t>(static_cast<uint32_t>(stack_base[sp - 1].i32)));
        DISPATCH();
    CASE_OP(I32TruncF32S)
        set_i32(sp - 1, trunc_f32_s(stack_base[sp - 1].f32));
        DISPATCH();
    CASE_OP(I32TruncF32U)
        set_i32(sp - 1, trunc_f32_u(stack_base[sp - 1].f32));
        DISPATCH();
    CASE_OP(I32TruncF64S)
        set_i32(sp - 1, trunc_f64_s(stack_base[sp - 1].f64));
        DISPATCH();
    CASE_OP(I32TruncF64U)
        set_i32(sp - 1, trunc_f64_u(stack_base[sp - 1].f64));
        DISPATCH();
    CASE_OP(I64TruncF32S)
        set_i64(sp - 1, trunc_f32_i64_s(stack_base[sp - 1].f32));
        DISPATCH();
    CASE_OP(I64TruncF32U)
        set_i64(sp - 1, trunc_f32_i64_u(stack_base[sp - 1].f32));
        DISPATCH();
    CASE_OP(I64TruncF64S)
        set_i64(sp - 1, trunc_f64_i64_s(stack_base[sp - 1].f64));
        DISPATCH();
    CASE_OP(I64TruncF64U)
        set_i64(sp - 1, trunc_f64_i64_u(stack_base[sp - 1].f64));
        DISPATCH();
    CASE_OP(F32ConvertI32S)
        set_f32(sp - 1, static_cast<float>(stack_base[sp - 1].i32));
        DISPATCH();
    CASE_OP(F32ConvertI32U)
        set_f32(sp - 1, static_cast<float>(static_cast<uint32_t>(stack_base[sp - 1].i32)));
        DISPATCH();
    CASE_OP(F32ConvertI64S)
        set_f32(sp - 1, static_cast<float>(stack_base[sp - 1].i64));
        DISPATCH();
    CASE_OP(F32ConvertI64U)
        set_f32(sp - 1, static_cast<float>(static_cast<uint64_t>(stack_base[sp - 1].i64)));
        DISPATCH();
    CASE_OP(F64ConvertI32S)
        set_f64(sp - 1, static_cast<double>(stack_base[sp - 1].i32));
        DISPATCH();
    CASE_OP(F64ConvertI32U)
        set_f64(sp - 1, static_cast<double>(static_cast<uint32_t>(stack_base[sp - 1].i32)));
        DISPATCH();
    CASE_OP(F64ConvertI64S)
        set_f64(sp - 1, static_cast<double>(stack_base[sp - 1].i64));
        DISPATCH();
    CASE_OP(F64ConvertI64U)
        set_f64(sp - 1, static_cast<double>(static_cast<uint64_t>(stack_base[sp - 1].i64)));
        DISPATCH();
    CASE_OP(F32DemoteF64)
        set_f32(sp - 1, static_cast<float>(stack_base[sp - 1].f64));
        DISPATCH();
    CASE_OP(F64PromoteF32)
        set_f64(sp - 1, static_cast<double>(stack_base[sp - 1].f32));
        DISPATCH();
    CASE_OP(I32ReinterpretF32)
        set_i32(sp - 1, std::bit_cast<int32_t>(stack_base[sp - 1].f32));
        DISPATCH();
    CASE_OP(I64ReinterpretF64)
        set_i64(sp - 1, std::bit_cast<int64_t>(stack_base[sp - 1].f64));
        DISPATCH();
    CASE_OP(F32ReinterpretI32)
        set_f32(sp - 1, std::bit_cast<float>(stack_base[sp - 1].i32));
        DISPATCH();
    CASE_OP(F64ReinterpretI64)
        set_f64(sp - 1, std::bit_cast<double>(stack_base[sp - 1].i64));
        DISPATCH();
    CASE_OP(I32Extend8S)
        set_i32(sp - 1, static_cast<int32_t>(static_cast<int8_t>(stack_base[sp - 1].i32)));
        DISPATCH();
    CASE_OP(I32Extend16S)
        set_i32(sp - 1, static_cast<int32_t>(static_cast<int16_t>(stack_base[sp - 1].i32)));
        DISPATCH();
    CASE_OP(I64Extend8S)
        set_i64(sp - 1, static_cast<int64_t>(static_cast<int8_t>(stack_base[sp - 1].i64)));
        DISPATCH();
    CASE_OP(I64Extend16S)
        set_i64(sp - 1, static_cast<int64_t>(static_cast<int16_t>(stack_base[sp - 1].i64)));
        DISPATCH();
    CASE_OP(I64Extend32S)
        set_i64(sp - 1, static_cast<int64_t>(static_cast<int32_t>(stack_base[sp - 1].i64)));
        DISPATCH();

    // --- Superinstructions ---

    CASE_OP(LocalIncI32)
        locals[ins->aux].i32 = static_cast<int32_t>(
            static_cast<uint32_t>(locals[ins->aux].i32) + static_cast<uint32_t>(ins->imm.i32));
        DISPATCH();

    CASE_OP(LocalGetAddI32C)
        set_i32(sp++, static_cast<int32_t>(
            static_cast<uint32_t>(locals[ins->aux].i32) + static_cast<uint32_t>(ins->imm.i32)));
        DISPATCH();

    CASE_OP(LocalGetSubI32C)
        set_i32(sp++, static_cast<int32_t>(
            static_cast<uint32_t>(locals[ins->aux].i32) - static_cast<uint32_t>(ins->imm.i32)));
        DISPATCH();

    CASE_OP(BrIfLocalGeI32C)
        if (locals[ins->aux].i32 >= ins->imm.i32) {
            pc = code_base + ins->aux2;
        }
        DISPATCH();

    CASE_OP(BrIfLocalGeS)
        if (locals[ins->aux].i32 >= locals[ins->aux2].i32) {
            pc = code_base + ins->imm.u32;
        }
        DISPATCH();

    CASE_OP(BrIfLocalGeU)
        if (static_cast<uint32_t>(locals[ins->aux].i32) >= static_cast<uint32_t>(locals[ins->aux2].i32)) {
            pc = code_base + ins->imm.u32;
        }
        DISPATCH();

    CASE_OP(LocalMemReadI32)
        set_i32(sp++, fat_read<int32_t>(locals[ins->aux].handle, static_cast<uint32_t>(locals[ins->imm.u32].i32)));
        DISPATCH();

    CASE_OP(LocalMemReadU8)
        set_i32(sp++, static_cast<int32_t>(fat_read<uint8_t>(locals[ins->aux].handle, static_cast<uint32_t>(locals[ins->imm.u32].i32))));
        DISPATCH();

    CASE_OP(I32AddC)
        stack_base[sp - 1].i32 = static_cast<int32_t>(
            static_cast<uint32_t>(stack_base[sp - 1].i32) + static_cast<uint32_t>(ins->imm.i32));
        DISPATCH();

    CASE_OP(I32SubC)
        stack_base[sp - 1].i32 = static_cast<int32_t>(
            static_cast<uint32_t>(stack_base[sp - 1].i32) - static_cast<uint32_t>(ins->imm.i32));
        DISPATCH();

    CASE_OP(I32MulC)
        stack_base[sp - 1].i32 = static_cast<int32_t>(
            static_cast<uint32_t>(stack_base[sp - 1].i32) * static_cast<uint32_t>(ins->imm.i32));
        DISPATCH();

    CASE_OP(I32ShrUC)
        stack_base[sp - 1].i32 = static_cast<int32_t>(
            static_cast<uint32_t>(stack_base[sp - 1].i32) >> (static_cast<uint32_t>(ins->imm.i32) & 31u));
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
