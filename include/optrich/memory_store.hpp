#pragma once

#include "optrich/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
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

    // Span view info (if rootHandle != kNullHandle, points to the ultimate owning root block)
    HandleId rootHandle = kNullHandle;
    uint32_t byteOffset = 0;
    uint32_t byteSize   = 0;
    uint32_t refOffset  = 0;
    uint32_t refCount   = 0;

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

    // Create a zero-copy byte sub-span backed by the same rootHandle, inheriting readOnly.
    // The new span has refCount = 0 (pure byte view).
    FatHandle make_span(HandleId h, uint32_t byte_off, uint32_t byte_len);

    // Create a zero-copy ref sub-span backed by the same rootHandle, inheriting readOnly.
    // The new span has byteSize = 0 (pure ref view).
    FatHandle make_ref_span(HandleId h, uint32_t ref_off, uint32_t ref_len);

    // Create a zero-copy read-only view of `h` preserving both byte and ref bounds.
    FatHandle as_readonly(HandleId h);

    // Resolve a HandleId into a FatHandle for pushing onto the operand stack.
    FatHandle to_fat_handle(HandleId h) const;

    // Metadata queries
    uint32_t byte_len(HandleId h) const;
    uint32_t ref_len(HandleId h) const;
    bool     is_readonly(HandleId h) const;
    bool     is_alive(HandleId h) const;
    HandleId root_of(HandleId h) const;

    // Reference-slot operations (bounds & readOnly checked)
    FatHandle read_ref(HandleId h, uint32_t slot_idx) const;
    void      write_ref(HandleId h, uint32_t slot_idx, HandleId child);

    // Direct byte read/write helpers (for host / C++ tests; VM uses FatHandle fast path)
    void read_bytes(HandleId h, uint32_t byte_off, void* dst, uint32_t len) const;
    void write_bytes(HandleId h, uint32_t byte_off, const void* src, uint32_t len);

    // Stats / inspection (also useful for future Phase 3 GC)
    size_t live_block_count() const { return liveCount_; }
    size_t total_slots() const { return objects_.size(); }

    const MemoryBlock& get_block(HandleId h) const;

private:
    MemoryBlock&       get_block_mut(HandleId h);
    const MemoryBlock& get_root(const MemoryBlock& blk) const;
    MemoryBlock&       get_root_mut(MemoryBlock& blk);
    HandleId           allocate_slot();

    std::vector<MemoryBlock> objects_;   // index 0 is reserved for kNullHandle
    std::vector<HandleId>    freeList_;  // recycled handle IDs (used by Phase 3 GC)
    size_t                   liveCount_ = 0;
};

} // namespace optrich
