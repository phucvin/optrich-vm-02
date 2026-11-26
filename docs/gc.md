# Garbage Collection Design

A core design question for adding GC to Optrich VM's handle/span memory model is: **because blocks are untyped raw byte buffers, how does the GC know which bytes inside a block are handles vs. ordinary numbers or text?**

This document summarizes the GC options considered and details **Split Blocks (Option 2a)**, the chosen primary design for `optrich-vm-02`.

---

## GC Options Considered

### 1. Conservative Mark-and-Sweep (Boehm-style)
* **How it works:** Scan the value stack, locals, and every aligned 32-bit word of every allocated block; any word matching a live handle ID marks that block (and for spans, its backing block).
* **Tradeoffs:** Simple to prototype, but suffers from false retention (ordinary integers pinning memory) and leaves handles as forgeable `i32` values.

### 2. Precise Tracing GC
To achieve exact collection and unforgeable handles while retaining low-level byte layouts:
* **2a. Split Blocks — `alloc(num_bytes, num_refs)`** *(selected for `optrich-vm-02`, detailed below)*: Separate raw byte storage (never scanned) from a handle-slot array (always scanned).
* **2b. Per-Block Pointer Bitmap — `alloc_with_layout(size, ptr_mask)`**: Allocation specifies a bitmask marking which offsets hold handles (like Go's GC heap bitmaps).
* **2c. CHERI-style 1-Bit Side Tags per Word**: Each 4-byte/8-byte word has a 1-bit `is_handle` tag set on `write_handle` and cleared on raw writes; traps on untagged `read_handle`. Maximum untyped flexibility at ~1.5–3% tag overhead.

### 3. Reference Counting (RC)
* **How it works:** Backing blocks carry a `refcount`; spans increment/decrement the backing block's count.
* **Tradeoffs:** Deterministic and pause-free, but still needs handle precision (2a/2b/2c), cannot reclaim cycles without a backup collector, and adds refcount traffic in hot loops.

### 4. Arenas / Regions (Zero-GC or Hybrid)
* **How it works:** Allocations belong to an `Arena` freed all at once in $O(1)$ (like Java Panama's `Arena`).
* **Tradeoffs:** Fastest allocation/reclamation and ideal for JIT hoisting, but not general-purpose for long-lived object graphs—best used as an optional complement to tracing GC.

---

## `make_span` Invariants

1. **Retaining the backing block:** Every span stores `rootHandle` + `offset` + `size` (not just a raw `uint8_t*`), ensuring that dropping the original handle while a span is reachable keeps the backing storage alive—and allowing future compaction by updating `objects[root].ptr`.
2. **Large-buffer retention:** Holding a tiny span into a large buffer keeps the backing buffer alive (like Go slices), so `clone_span` is provided to detach small views when needed.

---

## Chosen Design: Option 2a — Split Blocks (`alloc(num_bytes, num_refs)`)

Every allocated object is split into two disjoint regions:
1. **Raw bytes (`bytes`)** — for `i32`, `f64`, strings, flags, and numeric arrays. **Never scanned by GC.**
2. **Handle slots (`refs`)** — for references to other blocks/spans. **Always scanned by GC.**

Handles are a distinct `handle` value type on the VM stack (separate from `i32`), so integer arithmetic can never forge or corrupt a handle.

### Data Structure
```cpp
struct MemoryBlock {
    // Backing storage (empty if this block is a span)
    std::vector<uint8_t> bytes;
    std::vector<Handle>  refs;

    // Span view info (if rootHandle != 0, this is a span into rootHandle)
    Handle rootHandle = 0;
    size_t byteOffset = 0;
    size_t byteSize   = 0;
    size_t refOffset  = 0;
    size_t refCount   = 0;

    bool readOnly = false;
    bool marked   = false;
    bool alive    = true;
};
```
*(In an optimized allocator, `refs` and `bytes` can be placed back-to-back in a single contiguous buffer `[refs... | bytes...]` for cache locality and a single heap allocation per object.)*

### VM Operations
* `mem.alloc(num_bytes, num_refs) -> handle`
  * Plain byte buffer or string: `mem.alloc(N, 0)`
  * Array of object handles: `mem.alloc(0, M)`
  * Struct with primitives + references: `mem.alloc(N, M)`
* `mem.read_*` / `mem.write_*` — operates on `bytes` only.
* `mem.read_ref(h, slot_idx)` / `mem.write_ref(h, slot_idx, child_h)` — operates on `refs` only.
* `mem.span(h, byte_off, byte_len)` / `mem.readonly(h)` — zero-copy views inheriting `readOnly` and referencing `rootHandle`.

### Mark-and-Sweep Algorithm
1. **Roots:** Collect every `handle` from the shared operand stack, active call frames, module globals, string constants, and host-pinned handles into a worklist.
2. **Mark:**
   ```cpp
   while (!worklist.empty()) {
       Handle h = worklist.back(); worklist.pop_back();
       if (h == 0 || objects[h].marked) continue;
       MemoryBlock& blk = objects[h];
       blk.marked = true;

       if (blk.rootHandle != 0) {
           // Span: keep backing block alive
           worklist.push_back(blk.rootHandle);
       } else {
           // Owning block: scan ONLY refs; skip bytes completely
           for (Handle child : blk.refs) {
               if (child != 0) worklist.push_back(child);
           }
       }
   }
   ```
3. **Sweep:** Iterate `objects[1..]`. For any `alive && !marked` block, clear `bytes`/`refs`, mark `alive = false`, and push its handle ID onto a `freeList` for reuse. Reset `marked = false` on surviving blocks.

### Strengths & Tradeoffs
* **Strengths:** 100% precise GC + unforgeable capabilities without per-word tag bitmaps; $O(1)$ GC scanning for pure data buffers (`alloc(N, 0)`); clean compiler lowering (primitive fields → byte offsets, reference fields → ref indices).
* **Tradeoffs:** Structs containing handles cannot be shallow-copied with raw byte `memcpy` alone; codegen tracks two offset spaces (bytes vs. refs); spans over `refs` must decide between marking `rootHandle` (simple) vs. tracing only the sliced ref range.
