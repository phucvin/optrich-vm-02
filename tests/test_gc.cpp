#include "optrich/runtime.hpp"
#include "test_runner.hpp"

#include <cstdint>
#include <string>

using namespace optrich;

TEST(precise_gc_ignores_raw_bytes_no_false_retention) {
    MemoryStore store;
    store.set_gc_threshold(0); // manual GC

    FatHandle holder  = store.alloc(16, 0); // pure bytes, no refs
    FatHandle garbage = store.alloc(64, 0);
    HandleId garbage_id = garbage.id;

    // Write garbage's integer handle ID into holder's raw bytes.
    // A conservative GC would falsely retain `garbage`, but our split-block GC skips `bytes`!
    store.write_bytes(holder.id, 0, &garbage_id, sizeof(HandleId));

    GCStats stats = store.collect_garbage({holder.id});
    ASSERT_EQ(stats.marked, 1u);
    ASSERT_EQ(stats.swept, 1u);
    ASSERT_EQ(stats.liveAfter, 1u);
    ASSERT_TRUE(store.is_alive(holder.id));
    ASSERT_TRUE(!store.is_alive(garbage_id));
    ASSERT_EQ(store.free_list_size(), 1u);

    // Next allocation recycles the freed handle ID!
    FatHandle recycled = store.alloc(8, 0);
    ASSERT_EQ(recycled.id, garbage_id);
    ASSERT_EQ(store.free_list_size(), 0u);
}

TEST(cycle_collection_and_host_pinning) {
    MemoryStore store;
    store.set_gc_threshold(0);

    // Create a 2-node reference cycle: a <-> b
    FatHandle a = store.alloc(4, 1);
    FatHandle b = store.alloc(4, 1);
    store.write_ref(a.id, 0, b.id);
    store.write_ref(b.id, 0, a.id);

    // Pin `a`: both `a` and `b` stay alive even with empty external root set
    store.pin(a.id);
    GCStats s1 = store.collect_garbage({});
    ASSERT_EQ(s1.marked, 2u);
    ASSERT_EQ(s1.swept, 0u);
    ASSERT_TRUE(store.is_alive(a.id));
    ASSERT_TRUE(store.is_alive(b.id));

    // Unpin `a`: unreachable cycle is completely reclaimed!
    store.unpin(a.id);
    GCStats s2 = store.collect_garbage({});
    ASSERT_EQ(s2.marked, 0u);
    ASSERT_EQ(s2.swept, 2u);
    ASSERT_EQ(s2.liveAfter, 0u);
    ASSERT_TRUE(!store.is_alive(a.id));
    ASSERT_TRUE(!store.is_alive(b.id));
}

TEST(chained_spans_retain_root_and_clone_span_detaches) {
    MemoryStore store;
    store.set_gc_threshold(0);

    FatHandle big = store.alloc(4096, 0);
    const char* text = "abcdef_DETACH_ME_xyz";
    store.write_bytes(big.id, 100, text, 20);

    // Chained span: s2 -> s1 -> big
    FatHandle s1 = store.make_span(big.id, 100, 20);
    FatHandle s2 = store.make_span(s1.id, 7, 9); // "DETACH_ME"

    // Only root s2: s1 is swept, but `big` MUST remain alive because s2 points to rootHandle=big!
    GCStats g1 = store.collect_garbage({s2.id});
    ASSERT_TRUE(store.is_alive(s2.id));
    ASSERT_TRUE(store.is_alive(big.id));
    ASSERT_TRUE(!store.is_alive(s1.id));
    ASSERT_EQ(g1.swept, 1u);

    // Now detach s2 with clone_span
    FatHandle detached = store.clone_span(s2.id);
    ASSERT_EQ(store.root_of(detached.id), detached.id); // owns its own storage!
    ASSERT_EQ(detached.byteSize, 9u);

    // Only root `detached`: both s2 AND the 4096-byte `big` buffer are reclaimed!
    GCStats g2 = store.collect_garbage({detached.id});
    ASSERT_TRUE(store.is_alive(detached.id));
    ASSERT_TRUE(!store.is_alive(s2.id));
    ASSERT_TRUE(!store.is_alive(big.id));
    ASSERT_EQ(g2.swept, 2u);
    ASSERT_EQ(g2.liveAfter, 1u);

    char out[10] = {};
    store.read_bytes(detached.id, 0, out, 9);
    ASSERT_EQ(std::string(out), std::string("DETACH_ME"));
}

TEST(auto_gc_during_vm_execution_preserves_stack_roots) {
    // Aggressive GC threshold (16 blocks) while allocating 200 temporary blocks
    // and keeping a live accumulator block on the VM stack/local!
    Runtime rt(/*gc_threshold=*/16);
    rt.instantiate_wat("stress", R"(
    (module
      (func (export "churn") (param $iters i32) (result i32)
        (local $keep rw_handle)
        (local $tmp handle)
        (local $i i32)
        (local.set $keep (mem.alloc (i32.const 4) (i32.const 0)))
        (mem.write_i32 (local.get $keep) (i32.const 0) (i32.const 777))
        (block $done
          (loop $lp
            (br_if $done (i32.ge_s (local.get $i) (local.get $iters)))
            ;; Allocate temporary block + span + clone_span, then drop!
            (local.set $tmp (mem.alloc (i32.const 64) (i32.const 2)))
            (local.set $tmp (mem.span (local.get $tmp) (i32.const 8) (i32.const 16)))
            (local.set $tmp (mem.clone_span (local.get $tmp)))
            (local.set $tmp (handle.null))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br $lp)))
        (mem.read_i32 (local.get $keep) (i32.const 0)))
    )
    )");

    auto res = rt.invoke("stress", "churn", {Value::from_i32(200)});
    ASSERT_EQ(res[0].i32, 777);
    ASSERT_TRUE(rt.store().total_collections() > 0);

    // Final GC after invocation returns should sweep all remaining temporary blocks
    GCStats end = rt.collect_garbage();
    ASSERT_EQ(end.liveAfter, 0u);
}

TEST(cross_module_gc_roots_across_shared_store) {
    Runtime rt(/*gc_threshold=*/0);

    // Module A allocates a buffer and hands a read-only view to Module B
    rt.instantiate_wat("holder", R"(
    (module
      (global $saved (mut handle) (handle.null))
      (func (export "store_it") (param $h handle)
        (global.set $saved (local.get $h)))
      (func (export "clear_it")
        (global.set $saved (handle.null)))
      (func (export "read_saved") (result i32)
        (mem.read_i32 (global.get $saved) (i32.const 0)))
    )
    )");

    rt.instantiate_wat("producer", R"(
    (module
      (import "holder" "store_it" (func $store_it (param handle)))
      (func (export "make_and_share")
        (local $buf rw_handle)
        (local.set $buf (mem.alloc (i32.const 8) (i32.const 0)))
        (mem.write_i32 (local.get $buf) (i32.const 0) (i32.const 4242))
        ;; Share a read-only view with `holder`, then drop our own local
        (call $store_it (mem.readonly (local.get $buf))))
    )
    )");

    rt.invoke("producer", "make_and_share");

    // GC runs: `producer` has no reference, but `holder`'s global holds the ro view,
    // which keeps BOTH the ro view and the backing buffer alive!
    GCStats g1 = rt.collect_garbage();
    ASSERT_EQ(g1.liveAfter, 2u);
    ASSERT_EQ(rt.invoke("holder", "read_saved")[0].i32, 4242);

    // Clear `holder`'s global: both the ro view and backing buffer are swept!
    rt.invoke("holder", "clear_it");
    GCStats g2 = rt.collect_garbage();
    ASSERT_EQ(g2.swept, 2u);
    ASSERT_EQ(g2.liveAfter, 0u);
}

int main() {
    return optrich_test::run_all_tests();
}
