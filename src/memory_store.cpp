#include "optrich/memory_store.hpp"

#include <cstring>

namespace optrich {

MemoryStore::MemoryStore() {
    // Slot 0 is permanently reserved for kNullHandle.
    objects_.emplace_back();
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
    blk.readOnly = true;
    blk.marked = false;
    blk.alive = true;
    ++liveCount_;
    return to_fat_handle(id);
}

FatHandle MemoryStore::make_span(HandleId h, uint32_t byte_off, uint32_t byte_len) {
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
    const MemoryBlock& src = get_block(h);
    HandleId root = (src.rootHandle != kNullHandle) ? src.rootHandle : h;
    uint32_t b_off = src.byteOffset;
    uint32_t b_sz  = src.byteSize;
    uint32_t r_off = src.refOffset;
    uint32_t r_cnt = src.refCount;

    HandleId id = allocate_slot();
    MemoryBlock& view = objects_[id];
    view.rootHandle = root;
    view.byteOffset = b_off;
    view.byteSize = b_sz;
    view.refOffset = r_off;
    view.refCount = r_cnt;
    view.readOnly = true;
    view.marked = false;
    view.alive = true;
    ++liveCount_;
    return to_fat_handle(id);
}

uint32_t MemoryStore::byte_len(HandleId h) const {
    return get_block(h).byteSize;
}

uint32_t MemoryStore::ref_len(HandleId h) const {
    return get_block(h).refCount;
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

} // namespace optrich
