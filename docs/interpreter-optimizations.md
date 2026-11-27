# Interpreter Optimizations & Benchmarks

This document records the optimization passes applied to the `optrich-vm-02` bytecode interpreter, the measured before/after benchmark results, and the remaining headroom before introducing a JIT backend.

---

## 1. Baseline Bottlenecks

The initial clean-slate computed-goto interpreter (`make bench`, compiled with `g++ -std=c++20 -O2`) ran `fib(40)` in **52.866 s** vs. **1.174 s** for native C++ `-O2` (**45.0x** native), placing it in the classic stack-based interpreter tier (~30x–60x native).

Profiling the hot paths in `include/optrich/types.hpp`, `src/compiler.cpp`, and `src/vm.cpp` revealed four primary bottlenecks:

1. **32-byte `Value` copies on every stack/local op:** Because `Value` embeds `FatHandle` (24 bytes) and `FuncRef` (24 bytes) alongside a `ValType` tag, `sizeof(Value) == 32` bytes. Every `local.get`, `local.set`, `local.tee`, and `return` copied 32 bytes even for `i32`/`i64`/`f32`/`f64` values.
2. **Heavy per-call overhead in `dispatch_call` & `Return`:** Every recursive call checked `is_host()`, pushed/popped `std::vector<CallFrame>`, computed `locals.size() - paramCount`, and unconditionally reloaded `cur_inst`, `globals`, and `str_handles` even for intra-module calls.
3. **Pointer chasing on branches:** `do_branch` loaded `func->code.data()` on every jump instead of keeping `code_base` in a register.
4. **Pure stack dispatch:** Hot loops and `fib` dispatched many tiny 16-byte instructions (e.g., `local.get` + `i32.const` + `i32.add` + `local.set` = 4 indirect jumps per loop increment, and `Br -> Return` at the end of `if/else` arms).

---

## 2. Implemented Optimizations

### 2.1 Fast Intra-Module Calls (`CallLocal`) & Raw `CallFrame*` Stack
- Added `OpCode::CallLocal` in `include/optrich/bytecode.hpp`, emitted by `src/compiler.cpp` whenever `call` targets a function defined in the current module (`fidx >= funcImports.size()`).
- Cached `extraLocals` (`locals.size() - paramCount`) and `resultCount` (`sig.results.size()`) as `uint32_t` fields on `CompiledFunc`.
- Pre-sized `VM::frames_` to `maxCallDepth_` and switched from `std::vector::push_back`/`pop_back` to a raw `CallFrame* fp` pointer.
- Cached `code_base` (`func->code.data()`) and `funcs_base` (`cur_inst->module().funcs.data()`) in local registers inside `VM::invoke_index`.
- In `L_CallLocal`: skips `func_ref()` lookup, `is_host()` check, module pointer reloads, and skips local zero-initialization when `extraLocals == 0`.
- In `L_Return`: fast-paths `resultCount == 1` and only reloads `globals`, `str_handles`, and `funcs_base` when returning across module boundaries (`prev_inst != cur_inst`).

### 2.2 Typed Primitive Local & Return Copies
- Added `LocalGetPrim`, `LocalSetPrim`, and `LocalTeePrim`, emitted by the compiler for statically typed numeric locals (`i32`, `i64`, `f32`, `f64`).
- `LocalGetPrim` copies only `type` + `i64` (9 bytes instead of 32 bytes).
- `LocalSetPrim` and `LocalTeePrim` copy only `i64` (8 bytes), since the local's static `type` tag is already initialized when the frame is created.
- `Return` uses the same 9-byte primitive copy when returning a single numeric value.

### 2.3 Branch-Target-Safe Peephole Superinstructions
At the end of function compilation in `src/compiler.cpp`, `optimize_bytecode(CompiledFunc& fn)` collects all branch target PCs (`Br`, `BrIf`, `BrIfZero`, `BrTable`), fuses adjacent instructions only when no branch targets the interior of a fused sequence, and remaps all branch targets via `old_to_new`:

| Pattern | Fused Opcode | Savings |
| :--- | :--- | :--- |
| `Br(target, drop=0)` where `code[target] == Return` | `Return` | Eliminates jump-to-return in `if`/`else` |
| `LocalGetPrim(a) + I32Const(c) + I32Add + LocalSetPrim(a)` | `LocalIncI32` | 4 dispatches → 1 |
| `LocalGetPrim(a) + I32Const(c) + I32LtS + BrIfZero(target)` | `BrIfLocalGeI32C` | 4 dispatches → 1 |
| `LocalGetPrim(a) + LocalGetPrim(b) + I32GeS/U + BrIf(target)` | `BrIfLocalGeS` / `BrIfLocalGeU` | 4 dispatches → 1 |
| `LocalGet(buf) + LocalGetPrim(off) + MemReadI32/U8` | `LocalMemReadI32` / `LocalMemReadU8` | 3 dispatches → 1 |
| `LocalGetPrim(a) + I32Const(c) + I32Sub/Add` | `LocalGetSubI32C` / `LocalGetAddI32C` | 3 dispatches → 1 |
| `I32Const(c) + I32Add/Sub/Mul/ShrU` | `I32AddC` / `I32SubC` / `I32MulC` / `I32ShrUC` | 2 dispatches → 1 |

All fused instructions fit inside the existing 16-byte `Instr` layout (`op`, `aux`, `aux2`, `imm`).

---

## 3. Benchmark Results (`make bench`, `g++ -std=c++20 -O2`)

| Benchmark | Before | After | Speedup | Ratio vs Native `-O2` |
| :--- | ---: | ---: | ---: | ---: |
| **`fib(40)`** | 52.866 s | **20.318 s** | **2.6x** | **17.5x** *(was 45.0x)* |
| **`sum_loop(10M)`** | 3.593 s | **920.66 ms** | **3.9x** | **319.7x** *(was 1276.5x)* |
| **`sieve(100K)`** | 329.44 ms | **45.28 ms** | **7.3x** | — |
| **`array_sum(64K x 50)`** | 4.189 s | **225.32 ms** | **18.6x** | **81.5x** *(was 1501.3x)* |
| **`span_zero_copy(64KB x 20K)`** | 77.86 ms | **21.93 ms** | **3.6x** | — |
| **`span_copy_based(64KB x 20K)`** | 271.95 ms | **219.61 ms** | **1.2x** | — |
| **`dispatch_direct(5M)`** | 6.379 s | **596.33 ms** | **10.7x** | — |
| **`dispatch_indirect(5M)`** | 7.315 s | **1.210 s** | **6.0x** | — |
| **`dispatch_call_ref(5M)`** | 7.962 s | **1.568 s** | **5.1x** | — |
| **`gc_raw_bytes(20K x 4KB)`** | 61.91 ms | **27.29 ms** | **2.3x** | — |
| **`gc_ref_chain(100 x 1K)`** | 391.49 ms | **151.75 ms** | **2.6x** | — |

### Key Takeaways
- **`fib(40)`** improved from **45.0x** to **17.5x native `-O2`**, putting `optrich-vm-02` in the same performance tier as **WAMR Fast Interpreter** (~12x–20x) and **Wasmi 2.0** (~15x–25x), and within ~1.5x of **Wasm3** (~10x–15x).
- **Zero-copy span passing** is now **10.0x faster** than copy-based passing on 64 KB buffers (`21.93 ms` vs. `219.61 ms`), up from 3.5x before interpreter overhead was reduced.

---

## 4. Next Optimization Opportunities

1. **Compact internal stack slots (16 bytes or split 8-byte primitive + handle side-table):** Shrinking `VM`'s internal stack stride from 32 bytes to 16 bytes (packing `FatHandle` flags into the top bits of `HandleId` and tagging `FuncRef`'s pointer) halves L1 stack cache footprint.
2. **Direct-threaded code / tail-call dispatch (`[[clang::musttail]]`):** Replacing the `OpCode` jump-table index lookup with direct handler pointers (M3/Wasm3 style) eliminates one load per dispatch and improves branch prediction.
3. **Register-based bytecode:** Lowering stack slots to virtual registers eliminates push/pop traffic in arithmetic-heavy loops (`sum_loop`, `array_sum`).
4. **Baseline JIT:** Lowering resolved bytecode + static `rw_handle`/`ro_handle` types directly to machine code (e.g., Cranelift, asmjit, or LLVM ORC) with register-held `(ptr, byteSize)` fat handles and hoisted bounds checks to reach ~1.2x–2x native.
