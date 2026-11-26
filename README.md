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

## Architecture (Phases 1 & 2 Implemented)

* `include/optrich/types.hpp` — Value types (`i32`, `i64`, `f32`, `f64`, opaque `handle`, static `rw_handle` / `ro_handle`) and stack-resident `FatHandle`.
* `include/optrich/memory_store.hpp`, `src/memory_store.cpp` — Split-block `MemoryStore` separating raw `bytes` from GC-tracked `refs`, with flattened `rootHandle` zero-copy spans and `as_readonly`.
* `include/optrich/parser.hpp`, `src/parser.cpp` — WAT S-expression lexer & parser.
* `include/optrich/bytecode.hpp`, `include/optrich/compiler.hpp`, `src/compiler.cpp` — Compile/validation pass lowering `$names` to compact indices, precomputing branch target PCs, enforcing handle unforgeability and static `ro_handle` write safety, and failing fast on unknown opcodes.
* `include/optrich/vm.hpp`, `src/vm.cpp` — Flat-stack computed-goto interpreter with fat-handle fast paths for native `mem.*` opcodes.

## Documentation

* [Roadmap](docs/roadmap.md) — Phased implementation plan and MVP milestones for `optrich-vm-02`.
* [Memory Model & Performance](docs/memory-model.md) — Design space, industry prior art (MS-Wasm, Java `MemorySegment`, CHERI), and interpreter/JIT performance strategy.
* [Garbage Collection](docs/gc.md) — GC options and the Split Blocks (`alloc(num_bytes, num_refs)`) design.

## Status

**Milestone 1 (Phases 1 & 2) complete:** Capability-safe split-block memory model, complete core Wasm semantics, native `mem.*` opcodes, pre-resolved bytecode compiler, computed-goto VM with fat handles, CLI runner, and test suite. Next up: Phase 3 (Precise GC) and Phase 4 (Multi-Module Runtime & Linker). See [docs/roadmap.md](docs/roadmap.md) for details.
