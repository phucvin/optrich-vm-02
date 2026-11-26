#include "optrich/memory_store.hpp"
#include "test_runner.hpp"

#include <cstdint>
#include <cstring>

using namespace optrich;

TEST(alloc_split_block_zero_init) {
    MemoryStore store;
    FatHandle h = store.alloc(16, 4);

    ASSERT_TRUE(!h.is_null());
    ASSERT_TRUE(!h.is_readonly());
    ASSERT_EQ(h.byteSize, 16u);
    ASSERT_EQ(store.byte_len(h.id), 16u);
    ASSERT_EQ(store.ref_len(h.id), 4u);
    ASSERT_EQ(store.root_of(h.id), h.id);

    // Bytes are zero-initialized
    uint8_t buf[16];
    store.read_bytes(h.id, 0, buf, 16);
    for (int i = 0; i < 16; ++i) ASSERT_EQ(buf[i], 0);

    // Ref slots are null-initialized
    for (uint32_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(store.read_ref(h.id, i).is_null());
    }
}

TEST(bytes_and_refs_are_disjoint) {
    MemoryStore store;
    FatHandle parent = store.alloc(8, 2);
    FatHandle child  = store.alloc(4, 0);

    int32_t v = 0x12345678;
    store.write_bytes(parent.id, 0, &v, 4);
    store.write_ref(parent.id, 1, child.id);

    // Overwriting bytes does not touch refs
    int32_t v2 = -1;
    store.write_bytes(parent.id, 4, &v2, 4);

    ASSERT_EQ(store.read_ref(parent.id, 1).id, child.id);
    ASSERT_TRUE(store.read_ref(parent.id, 0).is_null());

    int32_t out = 0;
    store.read_bytes(parent.id, 0, &out, 4);
    ASSERT_EQ(out, 0x12345678);
}

TEST(zero_copy_span_and_nested_root_flattening) {
    MemoryStore store;
    FatHandle root = store.alloc(32, 2);
    const char* msg = "Hello, Optrich VM 02!";
    store.write_bytes(root.id, 0, msg, 21);

    // First-level span: "Optrich" (offset 7, len 7)
    FatHandle s1 = store.make_span(root.id, 7, 7);
    ASSERT_EQ(store.root_of(s1.id), root.id);
    ASSERT_EQ(s1.byteSize, 7u);
    ASSERT_EQ(store.ref_len(s1.id), 0u);
    ASSERT_TRUE(!s1.is_readonly());
    ASSERT_EQ(s1.ptr, root.ptr + 7); // zero-copy!

    // Nested span: "rich" (offset 3 within s1, len 4)
    FatHandle s2 = store.make_span(s1.id, 3, 4);
    ASSERT_EQ(store.root_of(s2.id), root.id); // flattened to ultimate root!
    ASSERT_EQ(s2.byteSize, 4u);
    ASSERT_EQ(s2.ptr, root.ptr + 10);

    char out[5] = {};
    store.read_bytes(s2.id, 0, out, 4);
    ASSERT_EQ(std::string(out), std::string("rich"));

    // Mutating through s2 mutates root in-place
    const char* upper = "RICH";
    store.write_bytes(s2.id, 0, upper, 4);
    char root_slice[8] = {};
    store.read_bytes(root.id, 7, root_slice, 7);
    ASSERT_EQ(std::string(root_slice), std::string("OptRICH"));
}

TEST(as_readonly_and_permission_inheritance) {
    MemoryStore store;
    FatHandle rw = store.alloc(16, 2);
    FatHandle child = store.alloc(4, 0);
    store.write_ref(rw.id, 0, child.id);

    FatHandle ro = store.as_readonly(rw.id);
    ASSERT_TRUE(ro.is_readonly());
    ASSERT_TRUE(!rw.is_readonly());
    ASSERT_EQ(store.root_of(ro.id), rw.id);
    ASSERT_EQ(store.byte_len(ro.id), 16u);
    ASSERT_EQ(store.ref_len(ro.id), 2u);
    ASSERT_EQ(store.read_ref(ro.id, 0).id, child.id);

    // Writes to ro fail
    int32_t x = 42;
    ASSERT_THROWS(store.write_bytes(ro.id, 0, &x, 4), Trap);
    ASSERT_THROWS(store.write_ref(ro.id, 0, kNullHandle), Trap);

    // Sub-spans of ro inherit readOnly
    FatHandle ro_span = store.make_span(ro.id, 4, 8);
    ASSERT_TRUE(ro_span.is_readonly());
    ASSERT_THROWS(store.write_bytes(ro_span.id, 0, &x, 4), Trap);

    FatHandle ro_ref_span = store.make_ref_span(ro.id, 0, 1);
    ASSERT_TRUE(ro_ref_span.is_readonly());
    ASSERT_EQ(store.read_ref(ro_ref_span.id, 0).id, child.id);
    ASSERT_THROWS(store.write_ref(ro_ref_span.id, 0, kNullHandle), Trap);
}

TEST(bounds_and_null_checks) {
    MemoryStore store;
    FatHandle h = store.alloc(8, 2);

    ASSERT_THROWS(store.make_span(h.id, 6, 4), Trap);
    ASSERT_THROWS(store.make_span(h.id, 0xFFFFFFFFu, 2), Trap); // overflow check
    ASSERT_THROWS(store.make_ref_span(h.id, 1, 2), Trap);
    ASSERT_THROWS(store.read_ref(h.id, 2), Trap);
    ASSERT_THROWS(store.write_ref(h.id, 2, kNullHandle), Trap);

    uint8_t b = 0;
    ASSERT_THROWS(store.read_bytes(kNullHandle, 0, &b, 1), Trap);
    ASSERT_THROWS(store.write_bytes(kNullHandle, 0, &b, 1), Trap);
    ASSERT_THROWS(store.make_span(kNullHandle, 0, 0), Trap);
    ASSERT_THROWS(store.as_readonly(kNullHandle), Trap);
}

int main() {
    return optrich_test::run_all_tests();
}
