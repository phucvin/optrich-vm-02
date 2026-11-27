# Optrich VM 02 Roadmap

**Goal:** Make the handle/span memory model **usable, safe, and fast** so sharing memory across WebAssembly-like modules is effortless and zero-copy.

> **Note:** `optrich-vm-02` is a **from-scratch rewrite**. No code is copied from `optrich-vm-01`; only applicable architectural ideas are reused.

---

## Phase 1: Capability-Safe Split-Block Memory Model
*Make sharing across modules safe and ergonomic by design.*

* **Opaque `handle` value type** (distinct from `i32`, with optional static `ro_handle` vs. `rw_handle`) so modules cannot forge or corrupt handles with integer arithmetic.
* **Split Blocks (`alloc(num_bytes, num_refs)`)** in `MemoryStore`: separate raw byte storage from GC-tracked handle slots, with `read_ref`/`write_ref` alongside typed byte reads/writes.
* **Safe zero-copy views:**
  * `make_span(h, byte_off, byte_len)` backed by `rootHandle` (never a raw dangling pointer) and inheriting `readOnly`.
  * `as_readonly(h)` to downgrade a mutable handle/span to read-only before passing it to another module.
  * `byte_len(h)` / `ref_len(h)` so callees can query bounds directly without passing separate length parameters.
* **Complete core Wasm semantics:** full numeric ops (`i32`, `i64`, `f32`, `f64`), `return`, `if`/`else`, typed local zero-initialization, strict lexical block/loop scoping, and fail-fast validation on unknown opcodes.

---

## Phase 2: Native Memory Opcodes & Fast Bytecode Interpreter
*Eliminate the primary performance bottlenecks of the `01` prototype.*

* **First-class memory opcodes:** `mem.alloc`, `mem.span`, `mem.readonly`, `mem.len`, `mem.read_*`, `mem.write_*`, `mem.read_ref`, `mem.write_ref`, plus bulk ops (`mem.copy`, `mem.fill`).
* **Compile / resolution pass:** Lower parsed WAT `$names` (locals, functions, types, globals, strings) to compact integer indices and precompute branch target PCs before execution.
* **Fast-path execution:**
  * Carry fat handles (or cached `(ptr, size, flags)`) on the operand stack to avoid per-access handle-table lookups.
  * Use computed-goto / threaded dispatch and a flat operand stack.

---

## Phase 3: Precise GC Across Shared Modules
*Support long-running multi-module programs without leaks or use-after-free.*

* **Precise mark-and-sweep GC** over the shared `MemoryStore` (see [docs/gc.md](gc.md)):
  * Trace roots across **all** module instances sharing the store (value stack, call frames, globals, string constants, host-pinned handles).
  * Mark only `refs` and span `rootHandle`s—skipping `bytes` completely ($O(1)$ marking for large strings/buffers).
  * Sweep unreachable blocks and recycle handle IDs via a `freeList`.
* **`clone_span(h)`** to detach a small slice from a large backing buffer when needed.
* *(Optional)* **Scoped arenas** for short-lived scratch allocations freed in $O(1)$ without waiting for GC.

---

## Phase 4: First-Class Multi-Module Runtime & Linker
*Make sharing memory and calling across modules seamless.*

* **Unified `Runtime` + `Linker`** hosting multiple `ModuleInstance`s over one shared `MemoryStore` and GC.
* **Direct cross-module calls:** Importing a function from another module pushes a `StackFrame` directly onto the shared call stack with zero host-wrapper boxing or copying.
* **Cross-module tables & `call_indirect`:** Enable passing interface/object handles across module boundaries where method references carry their target module instance.
* **Standardized shared conventions:** Define minimal conventions for shared read-only UTF-8 string spans, typed slices, and polymorphic interface handles.

---

## Phase 5: Benchmarks & Interpreter Optimization *(Complete)*
*Validate performance and optimize the bytecode interpreter.*

* **Benchmark suite (`make bench`):**
  1. Interpreter throughput on recursion (`fib(40)`), tight arithmetic loops (`sum_loop`, `sieve`), and fat-handle typed arrays (`array_sum`).
  2. Zero-copy span passing (`mem.span` + `mem.readonly`) vs. copy-based (`mem.alloc` + `mem.copy`) passing (**10.0x faster** on 64 KB buffers).
  3. Direct vs. `call_indirect` vs. `call_ref` vtable dispatch overhead.
  4. GC allocation and marking throughput on raw-byte-heavy vs. reference-heavy object graphs.
* **Interpreter optimizations** (see [docs/interpreter-optimizations.md](interpreter-optimizations.md)):
  * Intra-module `CallLocal`, raw `CallFrame*` stack, cached `code_base` / `funcs_base`, 8-byte primitive slot copies, and branch-target-safe peephole superinstructions (`fib(40)` reduced from 45.0x to **17.5x native `-O2`**).
* *(Deferred / Future)* **JIT backend:** Lowering resolved bytecode + static handle types to a baseline JIT (e.g., Cranelift, asmjit, or LLVM ORC) is deferred until after Optic is implemented.

---

## Phase 6: Optic (`.opc`) High-Level Language *(Next)*
*Design and build the Optrich-native high-level language targeting Optrich WAT directly (see [docs/high-level-language.md](high-level-language.md) and [docs/optic-design-workflow.md](optic-design-workflow.md)).*

* **Stage 6.0 — Spec-first design & golden `.guessed.wat` verification:** Iterate on `docs/optic-spec.md`, `.opc` examples, hand-lowered `.guessed.wat` files, and step-level `.expected.txt` golden tests via `host::trace_*` before implementing the compiler.

* **Frontend (`optic` lexer, parser, AST):** Clean recursive-descent parser for `.opc` modules (`fn`, `struct`, `trait`, `impl`, `let` / `let mut`, `if`/`else`, `while`, `for`, `import`/`export`).
* **Type checker & automatic split-block layout:**
  * Partition `struct` fields automatically into raw `bytes` offsets and GC-tracked `refs` slots (`mem.alloc(num_bytes, num_refs)`).
  * Enforce static read-only capabilities (`ro T`, `ro []u8`) and nullability (`T?`, `null`) at compile time.
* **Direct-to-WAT codegen:**
  * Zero-copy slices (`buf[start..end]` → `mem.span`, `clone(s)` → `mem.clone_span`, `readonly(x)` → `mem.readonly`).
  * Growable lists (`Vec<T>`), strings (`str` / `ro []u8`), closures, and polymorphic traits (`&dyn Trait` → read-only vtable blocks + `call_ref`, static generics → monomorphization).
  * Cross-module `import` / `export` linking directly over `Runtime` + `Linker`.
* **Tooling & examples:** CLI compilation (`optic` / `.opc` support in `optrich-vm`), end-to-end test suite, and Optic ports of core examples and benchmarks.

---

## MVP Milestones

1. **Milestone 1 (Core VM) — Done:** Phase 1 + Phase 2 — split blocks, native memory opcodes, resolved bytecode, and core test suite.
2. **Milestone 2 (Multi-Module Sharing) — Done:** Phase 4 — unified `Runtime`/`Linker`, direct cross-module calls, `call_indirect`/`call_ref` vtables, and zero-copy `as_readonly` sharing demos.
3. **Milestone 3 (Memory Management) — Done:** Phase 3 — precise cross-module GC with stress tests for cycles and chained spans.
4. **Milestone 4 (Performance Baseline) — Done:** Phase 5 — benchmark suite (`make bench`) and interpreter optimizations (`fib(40)` at 17.5x native `-O2`).
5. **Milestone 5 (Optic Language) — Next:** Phase 6 — `.opc` compiler emitting Optrich WAT with automatic split-block structs, zero-copy slices, static `ro` safety, and multi-module imports/exports.
6. **Future (Optional JIT):** Baseline JIT backend for compute-heavy workloads.
