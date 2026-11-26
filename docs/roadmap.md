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

## Phase 5: Benchmarks & JIT Readiness
*Validate performance and keep the architecture JIT-friendly.*

* **Benchmark suite:**
  1. Interpreter throughput on arithmetic, array loops, and string processing.
  2. Zero-copy cross-module buffer/span passing vs. copy-based passing.
  3. GC pause/throughput on raw-data-heavy vs. reference-heavy object graphs.
* **JIT-ready bytecode:** Ensure the resolved bytecode, static handle types, and structured control flow lower cleanly to a baseline JIT (e.g., Cranelift, asmjit, or LLVM ORC) with register-held `(ptr, size)` and hoisted bounds checks.

---

## MVP Milestones

1. **Milestone 1 (Core VM):** Phase 1 + Phase 2 — split blocks, native memory opcodes, resolved bytecode, and core test suite.
2. **Milestone 2 (Multi-Module Sharing):** Phase 4 — unified `Runtime`/`Linker`, direct cross-module calls, and zero-copy `as_readonly` sharing demos.
3. **Milestone 3 (Memory Management):** Phase 3 — precise cross-module GC with stress tests for cycles and chained spans.
4. **Milestone 4 (Performance & JIT):** Phase 5 — benchmarks and optional prototype JIT backend.
