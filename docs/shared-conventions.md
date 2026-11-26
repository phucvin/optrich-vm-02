# Standardized Shared Conventions (Multi-Module Interop)

Optrich VM 02 enables zero-copy sharing across modules over a single `MemoryStore` and `Runtime`. To keep cross-module interfaces simple and interoperable without a heavy schema/component marshaling layer, modules follow three minimal conventions:

## 1. Shared Read-Only UTF-8 String Spans (`ro_handle`)
* A UTF-8 string is represented directly as a `ro_handle` (or `handle` with `readOnly = true`) whose raw byte region contains UTF-8 bytes without a null terminator.
* **Length:** Queried directly via `mem.byte_len` (or `mem.len`) — no separate `(ptr, len)` pair is needed.
* **Substrings:** Created in $O(1)$ with zero copying via `mem.span(s, off, len)`, which inherits `ro_handle` / `readOnly`.
* **Detaching:** Call `mem.clone_span(s)` when retaining a tiny substring from a large backing buffer so GC can reclaim the backing buffer.

## 2. Typed Slices (`rw_handle` / `ro_handle`)
* A contiguous typed array/slice of `T` (e.g., `i32`, `f64`) is a split block `mem.alloc(count * sizeof(T), 0)` or a byte sub-span `mem.span(h, off * sizeof(T), sub_count * sizeof(T))`.
* Producers downgrade mutable buffers via `mem.readonly(buf)` before passing them to untrusted or read-only consumers.
* Reference arrays use `mem.alloc(0, count)` and `mem.ref_span(h, off, sub_count)`, with length queried via `mem.ref_len`.

## 3. Polymorphic Interface Handles (Trait Objects + Dynamic Private Vtables)
* Instead of embedding mutable integer tags inside an object's bytes or hardcoding static `(elem (i32.const N))` slots, Optrich VM 02 uses **Go/Rust-style trait objects** backed by private dynamic vtables (`ref.func` + `table.grow` + `(start $init)`):
  * Each interface module owns a **private** `(table $vtable 0 funcref)` and exports `register(fn: funcref) -> i32` (growing the table dynamically) and `wrap(data: handle, slot: i32) -> ro_handle`.
  * A trait object is a read-only split-block wrapper (`ro_handle`) with **`bytes[0..4)` = vtable slot** and **`refs[0]` = underlying data handle** (automatically traced by GC).
  * Underlying objects store pure data only, so one object can implement any number of independent interfaces with zero slot collisions or layout conflicts.
* See [docs/interfaces.md](interfaces.md) for the full design rationale and walkthrough.
