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

## 3. Polymorphic Interface Handles (Go/Rust-Style Trait Objects + `call_ref`)
* A trait object is a read-only 2-ref split block (`ro_handle` with 0 bytes, 2 refs):
  * **`refs[0]`**: the underlying pure-data `handle` (no vtable tag needed in its bytes).
  * **`refs[1]`**: a frozen read-only vtable handle (`mem.readonly(mem.alloc_vtable(N))`) holding unforgeable `funcref` methods (`mem.write_func` / `mem.read_func`).
* Callers dispatch directly via `call_ref (type $sig)`—no shared tables, no slot coordination, and full GC tracing of both `data` and `vtable`.
* See [docs/interfaces.md](interfaces.md) for the full design and walkthrough.
