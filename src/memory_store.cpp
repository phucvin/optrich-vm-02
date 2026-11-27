#include "optrich/memory_store.hpp"

#include <algorithm>
#include <cstring>

namespace optrich {

MemoryStore::MemoryStore() {
    // Slot 0 is permanently reserved for kNullHandle.
    objects_.emplace_back();
}

void MemoryStore::maybe_trigger_gc() {
    if (gcThreshold_ > 0 && liveCount_ >= gcThreshold_ && gcCallback_ && !inGc_) {
        inGc_ = true;
        gcCallback_();
        inGc_ = false;
        // Dynamically grow threshold if live set is large to avoid thrashing.
        if (liveCount_ >= gcThreshold_) {
            gcThreshold_ = std::max(gcThreshold_ * 2, liveCount_ * 2);
        }
    }
}

HandleId MemoryStore::allocate_slot() {
    if (!freeList_.empty()) {
        HandleId id = freeList_.back();
        freeList_.pop_back();
        objects_[id] = MemoryBlock{};
        return id;
    }
    HandleId id = static_cast<HandleId>(objects_.size());
    objects_.emplace_back();
    return id;
}

const MemoryBlock& MemoryStore::get_block(HandleId h) const {
    if (h == kNullHandle || h >= objects_.size() || !objects_[h].alive) {
        throw Trap("invalid or null handle access");
    }
    return objects_[h];
}

MemoryBlock& MemoryStore::get_block_mut(HandleId h) {
    if (h == kNullHandle || h >= objects_.size() || !objects_[h].alive) {
        throw Trap("invalid or null handle access");
    }
    return objects_[h];
}

const MemoryBlock& MemoryStore::get_root(const MemoryBlock& blk) const {
    if (blk.rootHandle == kNullHandle) {
        return blk;
    }
    return get_block(blk.rootHandle);
}

MemoryBlock& MemoryStore::get_root_mut(MemoryBlock& blk) {
    if (blk.rootHandle == kNullHandle) {
        return blk;
    }
    return get_block_mut(blk.rootHandle);
}

FatHandle MemoryStore::to_fat_handle(HandleId h) const {
    if (h == kNullHandle) {
        return FatHandle::null();
    }
    const MemoryBlock& blk = get_block(h);
    const MemoryBlock& root = get_root(blk);

    FatHandle fat;
    fat.id = h;
    fat.byteSize = blk.byteSize;
    fat.flags = kFlagValid | (blk.readOnly ? kFlagReadOnly : 0u);
    fat.ptr = (blk.byteSize > 0)
                  ? const_cast<uint8_t*>(root.bytes.data() + blk.byteOffset)
                  : nullptr;
    return fat;
}

FatHandle MemoryStore::alloc(uint32_t num_bytes, uint32_t num_refs, bool read_only) {
    maybe_trigger_gc();
    HandleId id = allocate_slot();
    MemoryBlock& blk = objects_[id];
    blk.bytes.assign(num_bytes, 0);
    blk.refs.assign(num_refs, kNullHandle);
    blk.rootHandle = kNullHandle;
    blk.byteOffset = 0;
    blk.byteSize = num_bytes;
    blk.refOffset = 0;
    blk.refCount = num_refs;
    blk.readOnly = read_only;
    blk.marked = false;
    blk.alive = true;
    ++liveCount_;
    return to_fat_handle(id);
}

FatHandle MemoryStore::alloc_readonly_bytes(std::string_view data) {
    maybe_trigger_gc();
    uint32_t len = static_cast<uint32_t>(data.size());
    HandleId id = allocate_slot();
    MemoryBlock& blk = objects_[id];
    blk.bytes.resize(len);
    if (len > 0) {
        std::memcpy(blk.bytes.data(), data.data(), len);
    }
    blk.rootHandle = kNullHandle;
    blk.byteOffset = 0;
    blk.byteSize = len;
    blk.refOffset = 0;
    blk.refCount = 0;
    blk.funcCount = 0;
    blk.readOnly = true;
    blk.marked = false;
    blk.alive = true;
    ++liveCount_;
    return to_fat_handle(id);
}

FatHandle MemoryStore::alloc_vtable(uint32_t num_funcs) {
    maybe_trigger_gc();
    HandleId id = allocate_slot();
    MemoryBlock& blk = objects_[id];
    blk.funcs.assign(num_funcs, FuncRef::null());
    blk.rootHandle = kNullHandle;
    blk.byteOffset = 0;
    blk.byteSize = 0;
    blk.refOffset = 0;
    blk.refCount = 0;
    blk.funcCount = num_funcs;
    blk.readOnly = false;
    blk.marked = false;
    blk.alive = true;
    ++liveCount_;
    return to_fat_handle(id);
}

FatHandle MemoryStore::make_span(HandleId h, uint32_t byte_off, uint32_t byte_len) {
    maybe_trigger_gc();
    const MemoryBlock& src = get_block(h);
    if (static_cast<uint64_t>(byte_off) + static_cast<uint64_t>(byte_len) > src.byteSize) {
        throw Trap("mem.span out of bounds");
    }

    HandleId root = (src.rootHandle != kNullHandle) ? src.rootHandle : h;
    uint32_t abs_off = src.byteOffset + byte_off;
    bool ro = src.readOnly;

    HandleId id = allocate_slot();
    MemoryBlock& span = objects_[id];
    span.rootHandle = root;
    span.byteOffset = abs_off;
    span.byteSize = byte_len;
    span.refOffset = 0;
    span.refCount = 0;
    span.readOnly = ro;
    span.marked = false;
    span.alive = true;
    ++liveCount_;
    return to_fat_handle(id);
}

FatHandle MemoryStore::make_ref_span(HandleId h, uint32_t ref_off, uint32_t ref_len) {
    maybe_trigger_gc();
    const MemoryBlock& src = get_block(h);
    if (static_cast<uint64_t>(ref_off) + static_cast<uint64_t>(ref_len) > src.refCount) {
        throw Trap("mem.ref_span out of bounds");
    }

    HandleId root = (src.rootHandle != kNullHandle) ? src.rootHandle : h;
    uint32_t abs_off = src.refOffset + ref_off;
    bool ro = src.readOnly;

    HandleId id = allocate_slot();
    MemoryBlock& span = objects_[id];
    span.rootHandle = root;
    span.byteOffset = 0;
    span.byteSize = 0;
    span.refOffset = abs_off;
    span.refCount = ref_len;
    span.readOnly = ro;
    span.marked = false;
    span.alive = true;
    ++liveCount_;
    return to_fat_handle(id);
}

FatHandle MemoryStore::as_readonly(HandleId h) {
    const MemoryBlock& existing = get_block(h);
    if (existing.readOnly) {
        return to_fat_handle(h);
    }
    maybe_trigger_gc();
    const MemoryBlock& src = get_block(h);
    HandleId root = (src.rootHandle != kNullHandle) ? src.rootHandle : h;
    uint32_t b_off = src.byteOffset;
    uint32_t b_sz  = src.byteSize;
    uint32_t r_off = src.refOffset;
    uint32_t r_cnt = src.refCount;
    uint32_t f_cnt = src.funcCount;

    HandleId id = allocate_slot();
    MemoryBlock& view = objects_[id];
    view.rootHandle = root;
    view.byteOffset = b_off;
    view.byteSize = b_sz;
    view.refOffset = r_off;
    view.refCount = r_cnt;
    view.funcCount = f_cnt;
    view.readOnly = true;
    view.marked = false;
    view.alive = true;
    ++liveCount_;
    return to_fat_handle(id);
}

FatHandle MemoryStore::clone_span(HandleId h) {
    maybe_trigger_gc();
    const MemoryBlock& src = get_block(h);
    const MemoryBlock& root = get_root(src);

    uint32_t b_off = src.byteOffset;
    uint32_t b_sz  = src.byteSize;
    uint32_t r_off = src.refOffset;
    uint32_t r_cnt = src.refCount;
    bool ro        = src.readOnly;

    std::vector<uint8_t> bytes_copy(b_sz);
    if (b_sz > 0) {
        std::memcpy(bytes_copy.data(), root.bytes.data() + b_off, b_sz);
    }
    std::vector<HandleId> refs_copy(r_cnt);
    if (r_cnt > 0) {
        std::copy_n(root.refs.data() + r_off, r_cnt, refs_copy.data());
    }

    HandleId id = allocate_slot();
    MemoryBlock& cloned = objects_[id];
    cloned.bytes = std::move(bytes_copy);
    cloned.refs = std::move(refs_copy);
    cloned.rootHandle = kNullHandle;
    cloned.byteOffset = 0;
    cloned.byteSize = b_sz;
    cloned.refOffset = 0;
    cloned.refCount = r_cnt;
    cloned.readOnly = ro;
    cloned.marked = false;
    cloned.alive = true;
    ++liveCount_;
    return to_fat_handle(id);
}

uint32_t MemoryStore::byte_len(HandleId h) const {
    return get_block(h).byteSize;
}

uint32_t MemoryStore::ref_len(HandleId h) const {
    return get_block(h).refCount;
}

uint32_t MemoryStore::func_len(HandleId h) const {
    return get_block(h).funcCount;
}

bool MemoryStore::is_readonly(HandleId h) const {
    return get_block(h).readOnly;
}

bool MemoryStore::is_alive(HandleId h) const {
    return h != kNullHandle && h < objects_.size() && objects_[h].alive;
}

HandleId MemoryStore::root_of(HandleId h) const {
    const MemoryBlock& blk = get_block(h);
    return (blk.rootHandle != kNullHandle) ? blk.rootHandle : h;
}

FatHandle MemoryStore::read_ref(HandleId h, uint32_t slot_idx) const {
    const MemoryBlock& blk = get_block(h);
    if (slot_idx >= blk.refCount) {
        throw Trap("mem.read_ref out of bounds");
    }
    const MemoryBlock& root = get_root(blk);
    HandleId child = root.refs[blk.refOffset + slot_idx];
    return to_fat_handle(child);
}

void MemoryStore::write_ref(HandleId h, uint32_t slot_idx, HandleId child) {
    MemoryBlock& blk = get_block_mut(h);
    if (blk.readOnly) {
        throw Trap("mem.write_ref on read-only handle");
    }
    if (slot_idx >= blk.refCount) {
        throw Trap("mem.write_ref out of bounds");
    }
    if (child != kNullHandle && !is_alive(child)) {
        throw Trap("mem.write_ref with invalid child handle");
    }
    MemoryBlock& root = get_root_mut(blk);
    root.refs[blk.refOffset + slot_idx] = child;
}

void MemoryStore::copy_refs(HandleId dst, uint32_t dst_off, HandleId src, uint32_t src_off, uint32_t count) {
    MemoryBlock& dst_blk = get_block_mut(dst);
    const MemoryBlock& src_blk = get_block(src);
    if (dst_blk.readOnly) {
        throw Trap("mem.copy_refs into read-only handle");
    }
    if (static_cast<uint64_t>(dst_off) + static_cast<uint64_t>(count) > dst_blk.refCount ||
        static_cast<uint64_t>(src_off) + static_cast<uint64_t>(count) > src_blk.refCount) {
        throw Trap("mem.copy_refs out of bounds");
    }
    if (count == 0) return;
    MemoryBlock& dst_root = get_root_mut(dst_blk);
    const MemoryBlock& src_root = get_root(src_blk);
    std::memmove(dst_root.refs.data() + dst_blk.refOffset + dst_off,
                 src_root.refs.data() + src_blk.refOffset + src_off,
                 static_cast<size_t>(count) * sizeof(HandleId));
}

FuncRef MemoryStore::read_func(HandleId h, uint32_t slot_idx) const {
    const MemoryBlock& blk = get_block(h);
    if (slot_idx >= blk.funcCount) {
        throw Trap("mem.read_func out of bounds");
    }
    const MemoryBlock& root = get_root(blk);
    return root.funcs[slot_idx];
}

void MemoryStore::write_func(HandleId h, uint32_t slot_idx, FuncRef fn) {
    MemoryBlock& blk = get_block_mut(h);
    if (blk.readOnly) {
        throw Trap("mem.write_func on read-only handle");
    }
    if (slot_idx >= blk.funcCount) {
        throw Trap("mem.write_func out of bounds");
    }
    MemoryBlock& root = get_root_mut(blk);
    root.funcs[slot_idx] = fn;
}

void MemoryStore::read_bytes(HandleId h, uint32_t byte_off, void* dst, uint32_t len) const {
    const MemoryBlock& blk = get_block(h);
    if (static_cast<uint64_t>(byte_off) + static_cast<uint64_t>(len) > blk.byteSize) {
        throw Trap("memory read out of bounds");
    }
    if (len == 0) return;
    const MemoryBlock& root = get_root(blk);
    std::memcpy(dst, root.bytes.data() + blk.byteOffset + byte_off, len);
}

void MemoryStore::write_bytes(HandleId h, uint32_t byte_off, const void* src, uint32_t len) {
    MemoryBlock& blk = get_block_mut(h);
    if (blk.readOnly) {
        throw Trap("memory write on read-only handle");
    }
    if (static_cast<uint64_t>(byte_off) + static_cast<uint64_t>(len) > blk.byteSize) {
        throw Trap("memory write out of bounds");
    }
    if (len == 0) return;
    MemoryBlock& root = get_root_mut(blk);
    std::memcpy(root.bytes.data() + blk.byteOffset + byte_off, src, len);
}

void MemoryStore::pin(HandleId h) {
    if (h == kNullHandle) return;
    (void)get_block(h); // validate alive
    ++pinned_[h];
}

void MemoryStore::unpin(HandleId h) {
    if (h == kNullHandle) return;
    auto it = pinned_.find(h);
    if (it != pinned_.end()) {
        if (--it->second == 0) {
            pinned_.erase(it);
        }
    }
}

bool MemoryStore::is_pinned(HandleId h) const {
    return pinned_.find(h) != pinned_.end();
}

GCStats MemoryStore::collect_garbage(const std::vector<HandleId>& roots) {
    GCStats stats;
    std::vector<HandleId> worklist;
    worklist.reserve(roots.size() + pinned_.size());

    for (HandleId h : roots) {
        if (h != kNullHandle) worklist.push_back(h);
    }
    for (const auto& [h, count] : pinned_) {
        if (h != kNullHandle && count > 0) worklist.push_back(h);
    }

    // Mark phase: trace ONLY span rootHandles and owning block refs (skip bytes completely!)
    while (!worklist.empty()) {
        HandleId h = worklist.back();
        worklist.pop_back();
        if (h == kNullHandle || h >= objects_.size()) continue;
        MemoryBlock& blk = objects_[h];
        if (!blk.alive || blk.marked) continue;

        blk.marked = true;
        ++stats.marked;

        if (blk.rootHandle != kNullHandle) {
            // Span: keep backing root block alive
            worklist.push_back(blk.rootHandle);
        } else {
            // Owning block: scan ONLY refs; skip bytes completely (O(1) for raw buffers)
            for (HandleId child : blk.refs) {
                if (child != kNullHandle) {
                    worklist.push_back(child);
                }
            }
        }
    }

    // Sweep phase: reclaim unmarked blocks and recycle handle IDs via freeList_
    for (HandleId h = 1; h < objects_.size(); ++h) {
        MemoryBlock& blk = objects_[h];
        if (!blk.alive) continue;

        if (!blk.marked) {
            blk.bytes.clear();
            blk.bytes.shrink_to_fit();
            blk.refs.clear();
            blk.refs.shrink_to_fit();
            blk.funcs.clear();
            blk.funcs.shrink_to_fit();
            blk.rootHandle = kNullHandle;
            blk.byteOffset = 0;
            blk.byteSize = 0;
            blk.refOffset = 0;
            blk.refCount = 0;
            blk.funcCount = 0;
            blk.readOnly = false;
            blk.alive = false;
            freeList_.push_back(h);
            --liveCount_;
            ++stats.swept;
        } else {
            blk.marked = false;
        }
    }

    ++totalCollections_;
    stats.liveAfter = liveCount_;
    stats.totalCollections = totalCollections_;
    return stats;
}

} // namespace optrich
