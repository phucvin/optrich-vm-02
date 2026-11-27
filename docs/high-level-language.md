# Compiling C & High-Level Languages to Optrich VM 02

This document analyzes the challenges of compiling **normal C** to Optrich VM 02's WAT/bytecode and capability-safe split-block memory model, compares architectural options for supporting C, and explains where a dedicated or adapted high-level language fits best.

---

## 1. Core Challenges of Compiling Normal C Natively

There is a fundamental difference between *emulating a flat C heap inside a sandboxed buffer* and *compiling C natively to Optrich's capability + split-block memory model*. Targeting the native memory model directly exposes several deep mismatches:

### A. Split Blocks (`bytes` vs. `refs`) Break C's Flat Object Layout
In `optrich-vm-02`, every allocation is a split block (`mem.alloc(num_bytes, num_refs)`) with two disjoint spaces: raw `bytes` (`mem.read_*` / `mem.write_*`) and GC-traced `refs` (`mem.read_ref` / `mem.write_ref`). Handles cannot be stored inside `bytes`.

Normal C assumes every object is a single flat array of bytes, which breaks:

* **Mixed structs:** `struct Node { int val; struct Node *next; };` must be split across two offset spaces (`bytes` for `val`, `refs` for `next`).
* **`sizeof`, `offsetof`, and `malloc(size_t)`:** `malloc(n)` only passes a byte count—it does not tell the VM `num_refs`. Lowering `malloc(sizeof(Node))` to `mem.alloc(num_bytes, num_refs)` requires knowing the static type at every allocation site, which breaks generic allocators, wrappers like `xmalloc`, and byte-generic containers (`qsort`, `bsearch`).
* **`memcpy` / `memmove` / `memset` / `void*`:** `mem.copy` only operates on `bytes`. Copying a struct containing pointers via `memcpy(dst, src, sizeof(T))` would silently drop or fail to copy `refs`, and `void*` erases whether a buffer contains references.
* **Unions mixing pointers and integers:** `union { intptr_t i; void *p; }` cannot be represented in split blocks without boxing or runtime tags.
* **Arrays of structs containing pointers:** `Node arr[10]` cannot be sliced into a single element handle with current span ops, because `mem.span` returns a pure byte view (`refCount = 0`) and `mem.ref_span` returns a pure ref view (`byteSize = 0`).

### B. C Pointers Are Integers; Optrich Handles Are Unforgeable Capabilities
* **Pointer arithmetic (`p++`, `p - 1`, `p1 - p2`, `p1 < p2`):** A C pointer cannot be lowered to just a `handle`, because `mem.span` can only *narrow* bounds (preventing `p - 1` or `container_of`), and allocating a new span block on every `p++` in a loop would be prohibitively slow.
* **Fat `(handle, i32 offset)` lowering:** Supporting pointer arithmetic requires lowering every C pointer to a `(handle, offset)` pair—doubling parameter/local/struct-field overhead and requiring `mem.eq` plus offset comparison for pointer equality.
* **Pointer-to-integer casts & bit-tagging:** `(uintptr_t)p`, low-bit pointer tagging, XOR linked lists, and hash-by-address are impossible because handles are opaque and strictly distinct from `i32`/`i64`.

### C. Address-Taken Locals (`&x`) and Stack Frames
Optrich locals (`local.get` / `local.set`) are not addressable. Any C local whose address is taken (`int x; foo(&x);` or `char buf[64]`) must be promoted to a heap-allocated `mem.alloc(...)` block (or a shadow stack block).

### D. Remaining Runtime/Data Differences for Full C
* **Static initialized data:** Currently only `(string ...)` constants and scalar/handle globals exist—no general binary data segment with static pointer relocations.
* **Manual `free()` / `realloc()`:** `free()` becomes a no-op under Optrich's precise GC, while `realloc()` requires allocating a new block and copying (invalidating existing aliases unless wrapped in an indirection).
* *(Note: `call_indirect` and function tables are already implemented as part of Phase 4, so C function pointers and vtables can be lowered cleanly.)*

### E. Toolchain Gap
Upstream Clang/LLVM assumes flat integer pointers in a single address space. Targeting Optrich's native handle/span model directly requires either emulating linear memory or maintaining a heavily modified compiler backend (such as CHERI-LLVM or MS-Wasm's LLVM fork).

---

## 2. Options to Solve It

### Option 1: "Linear Memory in a Handle" (Pragmatic C Compatibility)
Compile normal C with standard `clang --target=wasm32`, then run a lightweight Wasm-to-Optrich-WAT translator:

* Each C module allocates a single linear heap via `mem.alloc(heap_size, 0)` stored in a module global `$mem` (`rw_handle`).
* Standard Wasm `i32.load` / `i32.store` lower directly to `mem.read_* ($mem, addr)` / `mem.write_* ($mem, addr, val)`.
* Thin boundary intrinsics let C code **import/export zero-copy spans** (`mem.span($mem, ptr, len)` + `mem.readonly`) across module boundaries.

* **Pros:** 100% standard C works *today* (pointers, `malloc`, `memcpy`, unions, existing C libraries) using LLVM's full optimizer, while still participating in Optrich's zero-copy cross-module `ro_handle`/`rw_handle` sharing at module boundaries.
* **Cons:** *Inside* the C module, you lose per-object bounds checking, spatial safety, and precise GC (it behaves like Wasm MVP linear memory inside one block). Storing external Optrich handles inside C structs requires an explicit side handle-table (`mem.alloc(0, N)`).

### Option 2: Capability-Safe C (CHERI / MS-Wasm Style)
Lower C pointers to `(handle, offset)` and compile a disciplined subset of C (no pointer-to-integer casts, no out-of-bounds provenance tricks).

* **Critical catch for `optrich-vm-02`:** CHERI and MS-Wasm work on C *because* they use **tagged memory** (Option 2c in [docs/gc.md](gc.md): 1-bit `is_handle` side tags per word), allowing pointers and raw bytes to coexist in the same flat address space so `sizeof`, `memcpy`, `void*`, and `malloc(n)` continue to work.
* With **Split Blocks (Option 2a)**, even a capability-safe C dialect must ban untyped `malloc(n)`, `void*` `memcpy` over pointer-bearing structs, and mixed unions—at which point it is no longer "normal C".
* Pursuing Option 2 therefore requires either:
  1. Extending/changing the VM memory model to support tagged words (Option 2c) or per-block pointer bitmaps (Option 2b), *or*
  2. Imposing heavy C language restrictions plus maintaining a custom C frontend / LLVM fork.

### Option 3: A New (or Adapted) High-Level Language Built for Optrich's Model
Design a small, C-familiar language (or adapt a WasmGC-style language) whose semantics align directly with `optrich-vm-02`:

* **References vs. Values:** Managed structs/objects (allocated via `mem.alloc(num_bytes, num_refs)`) instead of raw `T*`. The compiler automatically partitions primitive fields into `bytes` and reference fields into `refs`—invisible to the programmer.
* **First-class Slices / Views:** `slice<T>` / `ro_slice<T>` (or `&[u8]` / `&mut [u8]`) map 1:1 to `rw_handle` / `ro_handle`, `mem.span`, `mem.readonly`, and `mem.byte_len`.
* **Typed Value Arrays vs. Reference Arrays:** `u8[]` / `i32[]` → `mem.alloc(len * elem_size, 0)`; `Object[]` → `mem.alloc(0, len)`.
* **Safe Semantics by Construction:** No `void*` `memcpy` over references, no pointer-to-integer casts, and no escaping `&local` (or explicit boxed cells).

* **Pros:** Zero impedance mismatch; full spatial safety, static `ro_handle` write guarantees, 100% precise split-block GC, and a dramatically simpler compiler (a clean recursive-descent frontend emitting Optrich WAT directly is a few thousand lines of code, vs. maintaining an LLVM fork).
* **Cons:** Existing C codebases cannot be compiled directly without porting.

### Option 4: Hybrid Architecture (Recommended)
Combine **Option 1 + Option 3**:

* **Existing / third-party C libraries** compile via `clang --target=wasm32` into a **Linear-in-a-Handle** module (`mem.alloc(N, 0)`), using explicit boundary intrinsics to pass and receive zero-copy `ro_handle`/`rw_handle` spans.
* **New native modules** are written in a **small Optrich-native language** (Option 3) that takes full advantage of split-block GC, per-object bounds, and static `ro_handle`/`rw_handle` safety.

Because `optrich-vm-02` is designed for **multi-module interoperability** (Phase 4), a linear-memory C module and a native split-block module can share byte buffers (`ro_handle` / `rw_handle` spans) with zero copying.

---

## 3. Chosen Direction: Option 3 — **Optic** (`.opc`)

We have selected **Option 3** as the primary high-level language strategy for `optrich-vm-02`, named **Optic** (`.opc`). Designing a compact language around the split-block capability model avoids the impedance mismatch of C while keeping the compiler small, predictable, and directly aligned with Optrich WAT.

### Core Language-to-VM Lowering Model

| Optic Feature | Optrich VM 02 Lowering |
| :--- | :--- |
| **Primitives** (`i32`, `i64`, `f32`, `f64`, `bool`, `u8`) | Native stack/local values (`i32`, `i64`, `f32`, `f64`) |
| **Managed `struct`s** (GC'd reference types) | Compiler partitions primitive fields into `bytes` offsets and reference fields into `refs` indices → `mem.alloc(num_bytes, num_refs)` |
| **Typed primitive slices & strings** (`[]u8`, `[]i32`, `ro []u8`, `str`) | `rw_handle` / `ro_handle` + `mem.span`, `mem.readonly`, `mem.byte_len`, `mem.read_*` / `mem.write_*` |
| **Reference arrays & growable lists** (`[]T`, `Vec<T>`) | `mem.alloc(0, len)` + `mem.read_ref` / `mem.write_ref`, or the header+backing indirection pattern (see `examples/ref_vec_demo.wat`) |
| **Read-only capabilities** (`ro T`, `ro []u8`, `readonly(x)`) | Compile-time `ro_handle` type + `mem.readonly` downgrade for zero-copy cross-module sharing |
| **Nullability** (`T?`, `null`) | `handle.null` + `mem.is_null` checks |
| **Interfaces & function values** (`trait`, `dyn Trait`, closures) | Phase 4 `table` + `call_indirect` + `(handle, vtable_idx)` pairs (see [docs/shared-conventions.md](shared-conventions.md)) |
| **Modules** (`import` / `export`) | Direct Optrich module `import`/`export` over the shared `Runtime` + `Linker` |

### Key Design Principles
1. **Automatic split-block layout:** Programmers declare ordinary structs mixing primitives and references; the Optic compiler computes byte offsets and ref slot indices transparently.
2. **Zero-copy slicing by default:** `buf[start..end]` lowers directly to `mem.span` (inheriting `ro`/`rw` permissions), with an explicit `clone(slice)` lowering to `mem.clone_span` when detaching from a large backing buffer.
3. **Static read-only safety:** Passing a mutable buffer across a module boundary as `ro []u8` emits `mem.readonly` once and lets the VM compiler reject writes at compile time.
4. **Simple direct-to-WAT compiler:** A recursive-descent frontend + type checker emitting Optrich WAT directly (no LLVM dependency required).

### Optic Example Code

#### Zero-Copy Token Splitter (`ro []u8` + `Vec` + `clone`)
```rust
export fn split_byte(input: ro []u8, sep: u8) -> Vec<ro []u8> {
    let out = Vec<ro []u8>::new(8);
    let mut start = 0;

    for i in 0..input.len {
        if input[i] == sep {
            out.push(input[start..i]);   // emits mem.span (zero-copy ro_handle)
            start = i + 1;
        }
    }
    out.push(input[start..input.len]);
    return out;
}

export fn extract_header(huge_packet: ro []u8) -> ro []u8 {
    let parts = split_byte(huge_packet, b'\n');
    return clone(parts[0]);              // emits mem.clone_span -> huge_packet can be GC'd
}
```

#### Mixed Structs & Nullable Links (Automatic Split-Block Layout)
```rust
struct Entry {
    id: i32,          // bytes[0..4)
    score: f64,       // bytes[4..12)
    name: ro []u8,    // refs[0]
    left: Entry?,     // refs[1]
    right: Entry?,    // refs[2]
}

fn find_score(node: Entry?, target_id: i32) -> f64 {
    let mut cur = node;
    while cur != null {
        if target_id == cur.id {
            return cur.score;
        }
        cur = if target_id < cur.id { cur.left } else { cur.right };
    }
    return -1.0;
}
```

#### Multi-Module Zero-Copy Sharing (`rw` → `ro` Downgrade)
```rust
// --- module: inspector ---
export fn count_zeros(view: ro []u8) -> i32 {
    let mut n = 0;
    for i in 0..view.len {
        if view[i] == 0 { n += 1; }
    }
    // view[0] = 1;  <-- compile-time error: cannot write to `ro []u8`
    return n;
}

// --- module: producer ---
import inspector::count_zeros;

export fn main() -> i32 {
    let buf = new [u8; 64];              // mem.alloc(64, 0) -> rw_handle
    buf[0] = 42;
    buf[1] = 0;
    buf[2] = 7;

    // Pass only the first 16 bytes as read-only (zero-copy mem.span + mem.readonly)
    return count_zeros(readonly(buf[0..16]));
}
```

#### Trait Dispatch (`dyn Trait` via Phase 4 `call_indirect`)
```rust
trait Hasher {
    fn feed(self, chunk: ro []u8);
    fn finish(self) -> i32;
}

struct XorHasher {
    acc: i32,
}

impl Hasher for XorHasher {
    fn feed(self, chunk: ro []u8) {
        for i in 0..chunk.len {
            self.acc ^= chunk[i] as i32;
        }
    }
    fn finish(self) -> i32 {
        return self.acc;
    }
}

export fn hash_all(h: dyn Hasher, chunks: ro []ro []u8) -> i32 {
    for i in 0..chunks.len {
        h.feed(chunks[i]);               // cross-module call_indirect
    }
    return h.finish();
}
```

---

## 4. Summary & Recommended Next VM Additions

| Goal | Best Path |
| :--- | :--- |
| **Run existing C code quickly** while keeping zero-copy cross-module spans | **Option 1** (`clang --target=wasm32` → Wasm-to-Optrich translator over a single `rw_handle` linear heap + boundary span intrinsics). |
| **Compile C natively with per-object safety** | Requires revisiting Split Blocks → **Option 2c (1-bit side tags)** in [docs/gc.md](gc.md), plus a capability-aware C compiler. |
| **Get the most out of `optrich-vm-02`'s split-block model** *(chosen)* | **Option 3** (a dedicated Optrich-native language with GC structs and `ro`/`rw` slices), optionally paired with Option 1 later for legacy C libraries (**Option 4**). |

### High-Leverage VM Additions for Compilers
Building on the existing Phase 4 `call_indirect` tables and multi-module `Runtime`, a few targeted VM additions will pay off immediately for the high-level language compiler:
1. **General data-segment initialization:** Extend `(string ...)` to arbitrary read-only/copyable byte blobs so static arrays and lookup tables do not have to be emitted as long sequences of `mem.write_*` instructions.
2. **Bulk reference copy (`mem.copy_refs`):** Mirror `mem.copy` for `refs` slots to accelerate growable `Vec<T>` reallocations.
3. **Paired byte+ref spans (optional):** If zero-copy slices over arrays of mixed `(bytes + refs)` structs are needed, consider a combined span or struct-of-arrays lowering convention.
