#pragma once

#include "optrich/types.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace optrich {

// Compact bytecode instruction set for the computed-goto interpreter.
enum class OpCode : uint16_t {
    // Control flow
    Nop = 0,
    Unreachable,
    Br,          // imm.u32 = target_pc
    BrIf,        // imm.u32 = target_pc (jump if top i32 != 0)
    BrIfZero,    // imm.u32 = target_pc (jump if top i32 == 0, used by `if`)
    BrTable,     // imm.u32 = br_table index in CompiledFunc::brTables
    Return,
    Call,        // imm.u32 = func index

    // Parametric
    Drop,
    Select,

    // Locals & Globals
    LocalGet,    // imm.u32 = local index
    LocalSet,    // imm.u32 = local index
    LocalTee,    // imm.u32 = local index
    GlobalGet,   // imm.u32 = global index
    GlobalSet,   // imm.u32 = global index

    // Constants
    I32Const,
    I64Const,
    F32Const,
    F64Const,
    HandleNull,
    MemConstStr, // imm.u32 = string constant index -> pushes readonly FatHandle

    // Native Split-Block Memory Opcodes (Phase 2)
    MemAlloc,      // (num_bytes: i32, num_refs: i32) -> rw_handle
    MemSpan,       // (h: handle, byte_off: i32, byte_len: i32) -> handle
    MemRefSpan,    // (h: handle, ref_off: i32, ref_len: i32) -> handle
    MemReadOnly,   // (h: handle) -> ro_handle
    MemByteLen,    // (h: handle) -> i32
    MemRefLen,     // (h: handle) -> i32
    MemIsReadOnly, // (h: handle) -> i32
    MemIsNull,     // (h: handle) -> i32
    MemEq,         // (h1: handle, h2: handle) -> i32

    // Fast-path typed byte reads (FatHandle on stack -> direct pointer load)
    MemReadU8,
    MemReadI8,
    MemReadU16,
    MemReadI16,
    MemReadI32,
    MemReadI64,
    MemReadF32,
    MemReadF64,

    // Fast-path typed byte writes (FatHandle on stack -> direct pointer store)
    MemWriteU8,
    MemWriteU16,
    MemWriteI32,
    MemWriteI64,
    MemWriteF32,
    MemWriteF64,

    // Reference-slot ops
    MemReadRef,    // (h: handle, slot_idx: i32) -> handle
    MemWriteRef,   // (h: handle, slot_idx: i32, child: handle) -> ()

    // Bulk memory ops
    MemCopy,       // (dst, dst_off, src, src_off, len) -> ()
    MemFill,       // (dst, dst_off, val, len) -> ()
    MemCmp,        // (a, a_off, b, b_off, len) -> i32

    // i32 comparisons & unary
    I32Eqz,
    I32Eq, I32Ne, I32LtS, I32LtU, I32GtS, I32GtU, I32LeS, I32LeU, I32GeS, I32GeU,
    I32Clz, I32Ctz, I32Popcnt,
    I32Add, I32Sub, I32Mul, I32DivS, I32DivU, I32RemS, I32RemU,
    I32And, I32Or, I32Xor, I32Shl, I32ShrS, I32ShrU, I32Rotl, I32Rotr,

    // i64 comparisons & arithmetic
    I64Eqz,
    I64Eq, I64Ne, I64LtS, I64LtU, I64GtS, I64GtU, I64LeS, I64LeU, I64GeS, I64GeU,
    I64Clz, I64Ctz, I64Popcnt,
    I64Add, I64Sub, I64Mul, I64DivS, I64DivU, I64RemS, I64RemU,
    I64And, I64Or, I64Xor, I64Shl, I64ShrS, I64ShrU, I64Rotl, I64Rotr,

    // f32 comparisons & arithmetic
    F32Eq, F32Ne, F32Lt, F32Gt, F32Le, F32Ge,
    F32Abs, F32Neg, F32Ceil, F32Floor, F32Trunc, F32Nearest, F32Sqrt,
    F32Add, F32Sub, F32Mul, F32Div, F32Min, F32Max, F32Copysign,

    // f64 comparisons & arithmetic
    F64Eq, F64Ne, F64Lt, F64Gt, F64Le, F64Ge,
    F64Abs, F64Neg, F64Ceil, F64Floor, F64Trunc, F64Nearest, F64Sqrt,
    F64Add, F64Sub, F64Mul, F64Div, F64Min, F64Max, F64Copysign,

    // Conversions & reinterpretations
    I32WrapI64,
    I64ExtendI32S, I64ExtendI32U,
    I32TruncF32S, I32TruncF32U, I32TruncF64S, I32TruncF64U,
    I64TruncF32S, I64TruncF32U, I64TruncF64S, I64TruncF64U,
    F32ConvertI32S, F32ConvertI32U, F32ConvertI64S, F32ConvertI64U,
    F64ConvertI32S, F64ConvertI32U, F64ConvertI64S, F64ConvertI64U,
    F32DemoteF64, F64PromoteF32,
    I32ReinterpretF32, I64ReinterpretF64, F32ReinterpretI32, F64ReinterpretI64,
    I32Extend8S, I32Extend16S, I64Extend8S, I64Extend16S, I64Extend32S,

    // Sentinel for dispatch table sizing
    Count
};

// Fixed-size 16-byte instruction: compact, cache-friendly, no heap indirection.
struct Instr {
    OpCode   op  = OpCode::Nop;
    uint16_t aux = 0; // optional secondary field (e.g., stack unwind arity/drop count for branches)
    uint32_t aux2 = 0; // optional extra field (e.g., target stack depth or result count)
    union {
        uint32_t u32;
        int32_t  i32;
        int64_t  i64;
        float    f32;
        double   f64;
    } imm = {0};

    static Instr make(OpCode op) {
        Instr ins;
        ins.op = op;
        return ins;
    }
    static Instr make_u32(OpCode op, uint32_t v) {
        Instr ins;
        ins.op = op;
        ins.imm.u32 = v;
        return ins;
    }
    static Instr make_i32(OpCode op, int32_t v) {
        Instr ins;
        ins.op = op;
        ins.imm.i32 = v;
        return ins;
    }
    static Instr make_i64(OpCode op, int64_t v) {
        Instr ins;
        ins.op = op;
        ins.imm.i64 = v;
        return ins;
    }
    static Instr make_f32(OpCode op, float v) {
        Instr ins;
        ins.op = op;
        ins.imm.f32 = v;
        return ins;
    }
    static Instr make_f64(OpCode op, double v) {
        Instr ins;
        ins.op = op;
        ins.imm.f64 = v;
        return ins;
    }
    static Instr make_branch(OpCode op, uint32_t target_pc, uint16_t arity, uint32_t drop_count) {
        Instr ins;
        ins.op = op;
        ins.aux = arity;
        ins.aux2 = drop_count;
        ins.imm.u32 = target_pc;
        return ins;
    }
};

struct BrTableTarget {
    uint32_t targetPc  = 0;
    uint16_t arity     = 0;
    uint32_t dropCount = 0;
};

struct BrTable {
    std::vector<BrTableTarget> targets;
    BrTableTarget              defaultTarget;
};

struct FuncSignature {
    std::vector<ValType> params;
    std::vector<ValType> results;
};

struct CompiledFunc {
    std::string          name;
    FuncSignature        sig;
    std::vector<ValType> locals;    // includes params first, then declared locals
    uint32_t             paramCount = 0;
    std::vector<Instr>   code;
    std::vector<BrTable> brTables;
};

struct CompiledGlobal {
    std::string name;
    ValType     type     = ValType::I32;
    bool        isMut    = false;
    Value       initVal;
    int32_t     initStrIdx = -1; // >= 0 if initialized from a string constant
};

struct CompiledModule {
    std::vector<FuncSignature>              types;
    std::vector<CompiledFunc>               funcs;
    std::vector<CompiledGlobal>             globals;
    std::vector<std::string>                strings; // pre-allocated as read-only handles on instantiation
    std::unordered_map<std::string, uint32_t> exports; // export name -> func index
};

} // namespace optrich
