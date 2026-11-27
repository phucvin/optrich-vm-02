# No-GC Considerations: Stepping Back from Tracing GC

This document steps back from the Split-Block tracing GC design in [docs/gc.md](gc.md) to evaluate:
1. Whether `optrich-vm-02` actually needs tracing GC at all,
2. The concrete architectural impact of replacing GC with **manual `mem.free()`**,
3. How to prevent handle forgery without GC, and
4. What actually breaks if handles are forgeable (even when host memory remains safe).

---

## 1. Do We Really Need Tracing GC?

In `optrich-vm-02`, the mark-and-sweep collector itself is small and fast (pure byte buffers `alloc(N, 0)` are $O(1)$ to scan). The **real architectural cost** of GC is that it drove the **Split Blocks (`bytes` vs. `refs`)** model, which:
* Prevents normal C from compiling natively (`sizeof`, `memcpy`, `void*`, unions; see [docs/high-level-language.md](high-level-language.md)).
* Requires two offset spaces (`bytes` and `refs`), `mem.copy_refs`, and `mem.sub_block`.
* Introduces stop-the-world pauses, root scanning, and non-deterministic reclamation.

What tracing GC **buys** is solving the **shared-ownership lifetime problem** created by zero-copy spans (`mem.span`, `mem.readonly`, `mem.sub_block`): once Module A hands a view of its buffer to Module B and Module B retains it in a struct, `Vec`, or closure, neither module knows when the backing block is safe to free.

### Comparison of Non-GC Alternatives

| Alternative | Eliminates Split Blocks? | Key Tradeoffs |
| :--- | :--- | :--- |
| **Scoped Arenas** (Java Panama `Arena` style) | **Yes** | Allocations live in an `Arena` freed in $O(1)$. Zero pauses and trivial JIT check hoisting. **Catch:** Poor fit for long-lived, dynamic mutable graphs (caches, `Vec`s, retained closures) without leaking until arena close or explicitly cloning into a longer-lived arena. |
| **Manual `free()` + Generational Handles** (MS-Wasm / Vale style) | **Yes** *(for GC; see Section 3 for unforgeability)* | Spatial + temporal safety without tracing. **Catch:** Breaks effortless cross-module retention (caller freeing early causes callee traps) and hurts JIT hoisting across calls. |
| **Reference Counting (RC / ARC)** | **No** | Still requires Split Blocks (or bitmaps/tags) so freeing a block knows which child handles to decrement; cycles leak; refcount traffic hits every stack/field write. |
| **Static Borrow Checking in Optic** (Rust style) | **Only in Optic, not the VM** | Trades a small VM GC for a Rust-grade borrow checker + cross-module lifetime ABI. A sandboxed VM still needs a runtime backstop (Arenas, generations, RC, or GC) unless bytecode itself proves lifetimes. |

### The Core Architectural Fork
* **General-purpose multi-module VM + Go/Swift-ergonomics Optic** *(current direction)*: Keep tracing GC. Effortless arbitrary object graphs, closures, and retained cross-module slices without a borrow checker require automatic lifetime management, and tracing GC beats RC here. *(Scoped Arenas can still be added later as an optional fast path.)*
* **Scoped Plugin / Request-Pipeline / FFI Sandbox** *(Cloudflare Workers, Envoy filters, Panama, eBPF)*: Drop GC and use **Scoped Arenas** (+ explicit `clone_span` to escape) or **Manual `free()` + Generational Handles**.

---

## 2. Deep Dive: Impact of Going with Manual `mem.free()`

Switching `optrich-vm-02` from tracing GC to explicit `mem.free()` affects five concrete areas of the VM and language.

### A. The `FatHandle` Fast Path Breaks (Temporal Safety)
Currently, `fat_read` and `fat_write` in `src/vm.cpp` dereference `h.ptr + off` directly with **zero handle-table lookup**. That is safe *only* because GC guarantees a reachable `FatHandle`'s backing `std::vector<uint8_t> bytes` can never be freed while that handle is live.

With `mem.free(h)`:
* If `h` is duplicated (another local, a global, a `refs` slot, or a `mem.span`) and freed via one copy, all other `FatHandle` copies still have `kFlagValid` set and a **dangling C++ `uint8_t* ptr`** — a real host use-after-free.
* **Required fix — Generational Handles:**
  1. Add `uint32_t generation` (or `uint64_t`) to `MemoryBlock` and `FatHandle` (along with `rootId`).
  2. `mem.free(h)` clears `bytes`/`refs`, sets `alive = false`, increments `generation++`, and recycles the slot via `freeList_`.
  3. Every dereference (`fat_read`, `fat_write`, `read_ref`, `write_ref`) must verify `objects_[h.rootId].generation == h.generation` (and `alive`), trapping deterministically on mismatch.
  4. **Ownership capability bit (`kFlagOwner`):** `mem.span`, `mem.sub_block`, and `mem.readonly` must clear `kFlagOwner` so a callee holding a borrowed/read-only view cannot `mem.free()` the caller's backing block.

### B. Spans Must Become Value-Type Fat Handles
Currently, `mem.span`, `mem.readonly`, and `mem.sub_block` allocate a `MemoryBlock` slot in `objects_`. Under GC, short-lived spans created in loops are automatically swept.

Under manual `free()`:
* Allocating a `MemoryBlock` slot per span would **leak slots forever** unless users manually freed every view.
* **Required fix:** Make spans **value-type fat views** carried directly on the stack (`{ ptr, byteSize, refOffset, refCount, flags, rootId, rootGen }`) with no `objects_` slot allocated.
  * *Upside:* Creating a span or `readonly` view becomes $O(1)$ register arithmetic with zero heap allocation.
  * *Downside:* Storing a handle/span inside another object now occupies ~16–24 bytes instead of a 4-byte `HandleId`.

### C. Interpreter & JIT Performance Tradeoffs
* **Interpreter:** Every `fat_read` / `fat_write` pays an extra dependent load (`objects_[h.rootId].generation == h.rootGen`) unless hoisted inside leaf loops.
* **Future JIT (larger impact):** Under GC or Arenas, a live handle in a register remains valid across function calls, enabling LICM and range-check hoisting across calls. Under manual `free()`, **any function call may free an aliased handle**, forcing the JIT to re-validate `generation` after calls (see [docs/memory-model.md](memory-model.md)).
* **Wins:** Zero GC pauses, deterministic memory usage, no root scanning, no `pinned_` map, and `Value` stack slots no longer need a runtime `ValType` tag for GC root discovery.

### D. Split Blocks vs. Native C Compatibility
Removing GC removes the *GC* reason for Split Blocks, **but not the capability-unforgeability reason** (see Section 3). To unlock native C (`malloc`, `free`, `sizeof`, `memcpy`, `void*`) without losing capability safety, Split Blocks would need to be replaced with **1-bit side tags per word** (CHERI / MS-Wasm style).

### E. Cross-Module Sharing & Optic (`.opc`) Ergonomics
* **Cross-module ownership contracts:** Functions like `split_byte(input: ro []u8, sep: u8) -> Vec<ro []u8>` require explicit C/Zig-style ownership rules (*"caller owns returned `Vec` and must free it"*, *"caller must keep `input` alive while `parts` is in use, or reads trap"*).
* **Optic becomes Zig-like instead of Go-like:** Instead of implicit GC structs and cycles, Optic would rely on explicit `free(x)` + `defer free(x)` (or RAII `drop`), giving **"Zig ergonomics with guaranteed runtime UAF traps instead of UB"**.

---

## 3. Preventing Handle Forgery Without GC

If handles are stored in raw `bytes`, untrusted code could overwrite those bytes with integers to forge a `(rootId, generation)` handle. Five mechanisms prevent this:

1. **Segregated Storage — Split Blocks (`bytes` vs. `refs`)** *(current VM)*: Raw byte writes can never touch `refs`, and the bytecode verifier ensures only `rw_handle` / `ro_handle` values can be written via `mem.write_ref`. Zero tag overhead, zero runtime checks; still incompatible with C's flat layout.
2. **1-Bit Side Tags per Slot (CHERI / MS-Wasm style)** *(best for flat memory / C)*: Keep a single flat `bytes` buffer plus a hidden parallel bitmap `tags` (1 bit per 8- or 16-byte aligned slot, ~0.8–1.5% overhead).
   * `mem.write_handle` (aligned) sets `tags[slot] = 1`.
   * Any raw write (`mem.write_*`, `mem.fill`) overlapping a slot clears `tags[slot] = 0`.
   * `mem.copy` copies both `bytes` and `tags` (clearing unaligned boundary slots).
   * `mem.read_handle` traps if `tags[slot] == 0`.
   * **Lazy `hasHandles` fast path:** Start every `MemoryBlock` with `hasHandles = false` and an empty `tags` vector; allocate `tags` only on the first `mem.write_handle`. Pure byte buffers pay zero tag memory and a single predicted branch on writes.
3. **Static Struct Layouts (WasmGC / JVM style):** Allocations declare a static field schema verified at compile time. Zero tag overhead, but loses untyped byte buffers and C `malloc(n)`.
4. **Per-Module Handle Tables (OS File Descriptors):** Memory stores plain `i32` indices into a private per-module table of handles. Prevents cross-module forgery, but adds indirection and breaks sharing structs containing handles across modules (slot `3` in Module A $\neq$ slot `3` in Module B).
5. **Cryptographic / Unpredictable Tokens (128-bit Random ID or MAC / ARM PAC):** Computationally infeasible to guess, but heavier per access, larger in memory, and vulnerable to side-channel leaks compared to tag bits inside a local VM.

---

## 4. What Breaks If Handles *Are* Forgeable?

If the forgeable value is an integer `HandleId` looked up in `MemoryStore::objects_` (bounds- and liveness-checked, **never** a raw `uint8_t*` pointer), **host memory cannot be corrupted**.

However, forging `HandleId`s inside a shared `MemoryStore` still breaks several core properties:

1. **Cross-module isolation collapses:** Untrusted Module B can loop `for id in 1..N` and read, overwrite, or `free` private blocks allocated by Module A (even with small sequential generation counters like `gen = 1`).
2. **`mem.readonly` and `mem.span` become bypassable:** If Module A creates a 16-byte read-only span (`id = 6`) from a 64 KB writable buffer (`id = 5`), Module B can simply guess `id = 5` and gain full read-write access to all 64 KB (and call `mem.free(5)`).
3. **Bug containment breaks (even for trusted modules):** A mundane integer bug (uninitialized variable, wrong local) in Module B can silently hit a live `HandleId` in Module A and corrupt its data instead of trapping.
4. **Optimizer / JIT alias analysis breaks:** The compiler can no longer assume a locally allocated, non-escaping buffer is invisible to callees, because any callee could synthesize its ID.
5. **No precise GC:** Integers and handles become indistinguishable in memory, forcing conservative scanning if GC is ever used.

### When Forgeable Handles *Are* Acceptable
Forgeable `HandleId`s are acceptable **only** when:
* Every module sharing a `MemoryStore` mutually trusts every other module (single-tenant program, not untrusted plugins), **and**
* The goal is strictly **host sandboxing + spatial/temporal crash safety** (no host memory corruption / C++ UB), rather than capability-enforced `ro_handle` / `mem.span` isolation between modules.

---

## Summary Matrix

| Dimension | Tracing GC (current) | Manual `free()` + Generational Handles | Scoped Arenas |
| :--- | :--- | :--- | :--- |
| **Host & temporal safety** | Guaranteed (no UAF) | Guaranteed (UAF → trap) | Guaranteed (arena check / scope) |
| **`fat_read` / `fat_write`** | Direct `ptr + off` | Check `objects_[rootId].gen == gen` | Direct `ptr + off` (within scope) |
| **Spans (`mem.span`)** | Heap `MemoryBlock` slot | Value-type fat handle | Value-type or arena-allocated |
| **JIT hoisting across calls** | Easy | Hard (calls may `free`) | Easy (within arena scope) |
| **GC pauses / root scanning** | Yes | None | None |
| **Cross-module retention** | Effortless | Ownership contracts / traps | Must `clone_span` to escape arena |
| **Optic ergonomics** | Go / Swift-like | Zig-like (`free` / `defer`) | Request/frame-scoped |
