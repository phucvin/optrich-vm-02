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

## 3. Polymorphic Interface Handles (Split Blocks + Cross-Module Tables)
* An interface/object handle is a split block whose:
  * **Bytes `[0..4)`** hold an `i32` vtable/method index into a shared `funcref` table (exported by an interface module and imported by implementers).
  * **Bytes `[4..)`** hold primitive instance fields, and **`refs[0..)`** hold GC-tracked child handles.
* Implementer modules populate their slots in the shared table via `(elem (i32.const slot) $method ...)`. Because each table entry (`FuncRef`) carries its owning `ModuleInstance*`, invoking `call_indirect` from any module switches module context and pushes a `CallFrame` directly onto the shared call stack with zero boxing or copying.
