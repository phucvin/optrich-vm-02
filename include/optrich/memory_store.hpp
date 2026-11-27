#pragma once

#include "optrich/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace optrich {

// Split-block memory allocation:
// - `bytes`: raw untyped byte storage (never scanned by GC)
// - `refs`:  GC-tracked handle slots (always scanned by GC)
// If `rootHandle != kNullHandle`, this block is a zero-copy span/view into `rootHandle`.
struct MemoryBlock {
    // Backing storage (empty if this block is a span)
    std::vector<uint8_t>  bytes;
    std::vector<HandleId> refs;
    std::vector<FuncRef>  funcs; // method slots for vtable blocks (empty for normal blocks)

    // Span view info (if rootHandle != kNullHandle, points to the ultimate owning root block)
    HandleId rootHandle = kNullHandle;
    uint32_t byteOffset = 0;
    uint32_t byteSize   = 0;
    uint32_t refOffset  = 0;
    uint32_t refCount   = 0;
    uint32_t funcCount  = 0;

    bool readOnly = false;
    bool marked   = false;
    bool alive    = false;
};

class MemoryStore {
public:
    MemoryStore();

    // Allocate a new owning split-block with `num_bytes` raw bytes (zero-initialized)
    // and `num_refs` handle slots (null-initialized).
    FatHandle alloc(uint32_t num_bytes, uint32_t num_refs, bool read_only = false);

    // Allocate a read-only byte block initialized from a string/byte slice.
    FatHandle alloc_readonly_bytes(std::string_view data);

    // Allocate a vtable block holding `num_funcs` unforgeable FuncRef method slots.
    FatHandle alloc_vtable(uint32_t num_funcs);

    // Create a zero-copy byte sub-span backed by the same rootHandle, inheriting readOnly.
    // The new span has refCount = 0 (pure byte view).
    FatHandle make_span(HandleId h, uint32_t byte_off, uint32_t byte_len);

    // Create a zero-copy ref sub-span backed by the same rootHandle, inheriting readOnly.
    // The new span has byteSize = 0 (pure ref view).
    FatHandle make_ref_span(HandleId h, uint32_t ref_off, uint32_t ref_len);

    // Create a combined zero-copy byte+ref sub-block view backed by the same rootHandle,
    // inheriting readOnly. Used for inlined nested structs and flat arrays of inline structs.
    FatHandle make_sub_block(HandleId h, uint32_t byte_off, uint32_t byte_len,
                             uint32_t ref_off, uint32_t ref_len);

    // Create a zero-copy read-only view of `h` preserving both byte and ref bounds.
    FatHandle as_readonly(HandleId h);

    // Detach a span (or block) into a fresh owning block containing only the sliced
    // bytes and refs, preserving readOnly. Allows the original backing buffer to be GC'd.
    FatHandle clone_span(HandleId h);

    // Resolve a HandleId into a FatHandle for pushing onto the operand stack.
    FatHandle to_fat_handle(HandleId h) const;

    // Metadata queries
    uint32_t byte_len(HandleId h) const;
    uint32_t ref_len(HandleId h) const;
    uint32_t func_len(HandleId h) const;
    bool     is_readonly(HandleId h) const;
    bool     is_alive(HandleId h) const;
    HandleId root_of(HandleId h) const;

    // Reference-slot operations (bounds & readOnly checked)
    FatHandle read_ref(HandleId h, uint32_t slot_idx) const;
    void      write_ref(HandleId h, uint32_t slot_idx, HandleId child);
    void      copy_refs(HandleId dst, uint32_t dst_off, HandleId src, uint32_t src_off, uint32_t count);

    // FuncRef vtable-slot operations (bounds & readOnly checked)
    FuncRef read_func(HandleId h, uint32_t slot_idx) const;
    void    write_func(HandleId h, uint32_t slot_idx, FuncRef fn);

    // Direct byte read/write helpers (for host / C++ tests; VM uses FatHandle fast path)
    void read_bytes(HandleId h, uint32_t byte_off, void* dst, uint32_t len) const;
    void write_bytes(HandleId h, uint32_t byte_off, const void* src, uint32_t len);

    // --- Phase 3: Precise Mark-and-Sweep GC ---

    // Host pinning (ref-counted): pinned handles are always treated as GC roots.
    void pin(HandleId h);
    void unpin(HandleId h);
    bool is_pinned(HandleId h) const;

    // Run precise mark-and-sweep GC given the external root set (stacks, frames, globals, strings).
    // Scans ONLY `refs` and span `rootHandle`s — skips `bytes` completely.
    GCStats collect_garbage(const std::vector<HandleId>& roots);

    // Automatic GC trigger configuration.
    // When `gcThreshold_ > 0` and `liveCount_ >= gcThreshold_`, `gcCallback_` is invoked
    // before new allocations. Set to 0 to disable automatic GC.
    void   set_gc_threshold(size_t threshold) { gcThreshold_ = threshold; }
    size_t gc_threshold() const { return gcThreshold_; }
    void   set_gc_callback(std::function<void()> cb) { gcCallback_ = std::move(cb); }

    // Stats / inspection
    size_t live_block_count() const { return liveCount_; }
    size_t total_slots() const { return objects_.size(); }
    size_t free_list_size() const { return freeList_.size(); }
    size_t total_collections() const { return totalCollections_; }

    const MemoryBlock& get_block(HandleId h) const;

private:
    MemoryBlock&       get_block_mut(HandleId h);
    const MemoryBlock& get_root(const MemoryBlock& blk) const;
    MemoryBlock&       get_root_mut(MemoryBlock& blk);
    HandleId           allocate_slot();
    void               maybe_trigger_gc();

    std::vector<MemoryBlock>             objects_;   // index 0 is reserved for kNullHandle
    std::vector<HandleId>                freeList_;  // recycled handle IDs
    std::unordered_map<HandleId, uint32_t> pinned_;  // host-pinned handle refcounts
    size_t                               liveCount_        = 0;
    size_t                               totalCollections_ = 0;
    size_t                               gcThreshold_      = 4096;
    bool                                 inGc_             = false;
    std::function<void()>                gcCallback_;
};

} // namespace optrich
