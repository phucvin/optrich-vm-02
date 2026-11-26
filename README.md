# Optrich VM 02

A clean-slate WebAssembly-inspired virtual machine designed around a **capability-safe, handle-and-span memory model** for fast, zero-copy memory sharing across modules.

> **Clean-Slate Rewrite:** `optrich-vm-02` is rewritten **from scratch**. It does not reuse source code from [`optrich-vm-01`](https://github.com/phucvin/optrich-vm-01); it carries forward only the validated design ideas (handle-based memory, zero-copy permission-inheriting spans, and multi-module execution) on top of a new, performance-oriented architecture.

## Goals

* **Usable cross-module memory sharing:** Share mutable and read-only views (`make_span`, `as_readonly`) across Wasm modules with zero copying and strong capability isolation.
* **Fast execution:** Native memory opcodes, pre-resolved bytecode (no runtime string lookups), fat-handle fast paths, and an IR designed to lower cleanly to a JIT.
* **Safe lifetimes:** Split-block allocations (`alloc(num_bytes, num_refs)`) enabling simple, 100% precise garbage collection across all modules sharing a store.

## Documentation

* [Roadmap](docs/roadmap.md) — Phased implementation plan and MVP milestones for `optrich-vm-02`.
* [Memory Model & Performance](docs/memory-model.md) — Design space, industry prior art (MS-Wasm, Java `MemorySegment`, CHERI), and interpreter/JIT performance strategy.
* [Garbage Collection](docs/gc.md) — GC options and the Split Blocks (`alloc(num_bytes, num_refs)`) design.

## Status

Initial design and planning phase. See [docs/roadmap.md](docs/roadmap.md) for upcoming milestones.
