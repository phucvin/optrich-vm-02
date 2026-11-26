# Optrich VM 02

A clean-slate WebAssembly-inspired virtual machine designed around a **capability-safe, handle-and-span memory model** for fast, zero-copy memory sharing across modules.

> **Clean-Slate Rewrite:** `optrich-vm-02` is rewritten **from scratch**. It does not reuse source code from [`optrich-vm-01`](https://github.com/phucvin/optrich-vm-01); it carries forward only the validated design ideas (handle-based memory, zero-copy permission-inheriting spans, and multi-module execution) on top of a new, performance-oriented architecture.

## Goals

* **Usable cross-module memory sharing:** Share mutable and read-only views (`mem.span`, `mem.readonly`) across Wasm modules with zero copying and strong capability isolation.
* **Fast execution:** Native memory opcodes, pre-resolved bytecode (no runtime string lookups), fat-handle fast paths, and an IR designed to lower cleanly to a JIT.
* **Safe lifetimes:** Split-block allocations (`mem.alloc(num_bytes, num_refs)`) enabling simple, 100% precise garbage collection across all modules sharing a store.

## Building & Testing

Requires a C++20 compiler (`g++`) and `make`.

```bash
# Build the CLI runner (build/optrich-vm)
make

# Build and run all unit & integration test suites
make test

# Run an example WAT module
./build/optrich-vm examples/span_demo.wat main
```

## Architecture (Phases 1–4 Implemented)

* `include/optrich/types.hpp` — Tagged `Value`s (`i32`, `i64`, `f32`, `f64`, opaque `handle`, static `rw_handle` / `ro_handle`), `FatHandle`, and `GCStats`.
* `include/optrich/memory_store.hpp`, `src/memory_store.cpp` — Split-block `MemoryStore` separating raw `bytes` from GC-tracked `refs`, flattened `rootHandle` zero-copy spans, `as_readonly`, `clone_span`, host pinning, and precise mark-and-sweep GC ($O(1)$ on raw byte buffers).
* `include/optrich/parser.hpp`, `src/parser.cpp` — WAT S-expression lexer & parser.
* `include/optrich/bytecode.hpp`, `include/optrich/compiler.hpp`, `src/compiler.cpp` — Compile/validation pass lowering `$names` to compact indices, precomputing branch target PCs, supporting imports/tables/`elem`/`call_indirect`, and enforcing handle unforgeability and static `ro_handle` write safety.
* `include/optrich/vm.hpp`, `src/vm.cpp` — Flat-stack computed-goto interpreter with fat-handle fast paths, direct cross-module calls, cross-module `call_indirect`, and precise stack/frame GC root scanning.
* `include/optrich/runtime.hpp`, `src/runtime.cpp` — Unified multi-module `Runtime` and `Linker` over a single shared `MemoryStore` and cross-module GC.

## Documentation

* [Roadmap](docs/roadmap.md) — Phased implementation plan and MVP milestones for `optrich-vm-02`.
* [Memory Model & Performance](docs/memory-model.md) — Design space, industry prior art (MS-Wasm, Java `MemorySegment`, CHERI), and interpreter/JIT performance strategy.
* [Garbage Collection](docs/gc.md) — GC options and the Split Blocks (`alloc(num_bytes, num_refs)`) design.
* [Shared Conventions](docs/shared-conventions.md) — Minimal conventions for shared UTF-8 string spans, typed slices, and polymorphic interface handles.

## Status

**Phases 1–4 (Milestones 1–3) complete:** Capability-safe split-block memory model, complete core Wasm semantics, native `mem.*` opcodes, pre-resolved bytecode compiler, computed-goto VM with fat handles, precise cross-module mark-and-sweep GC (`clone_span`, host pinning, automatic threshold triggers), and unified multi-module `Runtime` + `Linker` with direct cross-module calls and `call_indirect` tables. Next up: Phase 5 (Benchmarks & JIT Readiness). See [docs/roadmap.md](docs/roadmap.md) for details.
