#include "optrich/compiler.hpp"
#include "optrich/memory_store.hpp"
#include "optrich/vm.hpp"
#include "test_runner.hpp"

using namespace optrich;

TEST(native_typed_read_write_and_lengths) {
    const char* wat = R"(
    (module
      (func (export "test") (result i32)
        (local $h rw_handle)
        (local.set $h (mem.alloc (i32.const 32) (i32.const 3)))
        (if (i32.ne (mem.byte_len (local.get $h)) (i32.const 32)) (then (return (i32.const -1))))
        (if (i32.ne (mem.ref_len (local.get $h)) (i32.const 3)) (then (return (i32.const -2))))

        (mem.write_u8  (local.get $h) (i32.const 0)  (i32.const 250))
        (mem.write_u16 (local.get $h) (i32.const 2)  (i32.const 50000))
        (mem.write_i32 (local.get $h) (i32.const 4)  (i32.const -123456))
        (mem.write_i64 (local.get $h) (i32.const 8)  (i64.const 9876543210))
        (mem.write_f32 (local.get $h) (i32.const 16) (f32.const 1.5))
        (mem.write_f64 (local.get $h) (i32.const 24) (f64.const 2.25))

        (if (i32.ne (mem.read_u8  (local.get $h) (i32.const 0)) (i32.const 250)) (then (return (i32.const -4))))
        (if (i32.ne (mem.read_i8  (local.get $h) (i32.const 0)) (i32.const -6))  (then (return (i32.const -5))))
        (if (i32.ne (mem.read_u16 (local.get $h) (i32.const 2)) (i32.const 50000)) (then (return (i32.const -6))))
        (if (i32.ne (mem.read_i32 (local.get $h) (i32.const 4)) (i32.const -123456)) (then (return (i32.const -7))))
        (if (i64.ne (mem.read_i64 (local.get $h) (i32.const 8)) (i64.const 9876543210)) (then (return (i32.const -8))))
        (if (f32.ne (mem.read_f32 (local.get $h) (i32.const 16)) (f32.const 1.5)) (then (return (i32.const -9))))
        (if (f64.ne (mem.read_f64 (local.get $h) (i32.const 24)) (f64.const 2.25)) (then (return (i32.const -10))))
        (i32.const 1))
    )
    )";
    CompiledModule mod = compile_wat(wat);
    MemoryStore store;
    ModuleInstance inst(std::move(mod), store);
    VM vm;
    ASSERT_EQ(vm.invoke(inst, "test")[0].i32, 1);
}

TEST(zero_copy_spans_readonly_and_const_strings) {
    const char* wat = R"(
    (module
      (string $greeting "Hello, Optrich!")

      (func $sum_bytes (param $s ro_handle) (result i32)
        (local $i i32)
        (local $n i32)
        (local $acc i32)
        (local.set $n (mem.len (local.get $s)))
        (block $done
          (loop $lp
            (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
            (local.set $acc (i32.add (local.get $acc) (mem.read_u8 (local.get $s) (local.get $i))))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br $lp)))
        (local.get $acc))

      ;; Zero-copy slice of a string constant
      (func (export "slice_sum") (result i32)
        (local $s ro_handle)
        (local $sub ro_handle)
        (local.set $s (mem.const_str $greeting))
        ;; "Optrich" = offset 7, len 7
        (local.set $sub (mem.span (local.get $s) (i32.const 7) (i32.const 7)))
        (call $sum_bytes (local.get $sub)))

      ;; Downgrade rw_handle to ro_handle and pass to reader
      (func (export "share_readonly") (result i32)
        (local $buf rw_handle)
        (local $ro ro_handle)
        (local.set $buf (mem.alloc (i32.const 4) (i32.const 0)))
        (mem.write_u8 (local.get $buf) (i32.const 0) (i32.const 10))
        (mem.write_u8 (local.get $buf) (i32.const 1) (i32.const 20))
        (mem.write_u8 (local.get $buf) (i32.const 2) (i32.const 30))
        (mem.write_u8 (local.get $buf) (i32.const 3) (i32.const 40))
        (local.set $ro (mem.readonly (local.get $buf)))
        ;; Mutate through original rw handle -> visible through zero-copy ro handle!
        (mem.write_u8 (local.get $buf) (i32.const 3) (i32.const 50))
        (call $sum_bytes (local.get $ro)))

      ;; Attempting to read a read-only ref slot as rw_handle traps at runtime!
      (func (export "read_ref_rw_on_ro")
        (local $box rw_handle)
        (local $rw rw_handle)
        (local.set $box (mem.alloc (i32.const 0) (i32.const 1)))
        (mem.write_ref (local.get $box) (i32.const 0) (mem.readonly (mem.alloc (i32.const 8) (i32.const 0))))
        (local.set $rw (mem.read_ref_rw (local.get $box) (i32.const 0)))
        (mem.write_i32 (local.get $rw) (i32.const 0) (i32.const 99)))
    )
    )";
    CompiledModule mod = compile_wat(wat);
    MemoryStore store;
    ModuleInstance inst(std::move(mod), store);
    VM vm;

    // Sum of ASCII bytes of "Optrich"
    int expected = 'O' + 'p' + 't' + 'r' + 'i' + 'c' + 'h';
    ASSERT_EQ(vm.invoke(inst, "slice_sum")[0].i32, expected);
    ASSERT_EQ(vm.invoke(inst, "share_readonly")[0].i32, 10 + 20 + 30 + 50);
    ASSERT_THROWS(vm.invoke(inst, "read_ref_rw_on_ro"), Trap);
}

TEST(static_ro_handle_compile_time_rejection) {
    // Writing directly to ro_handle is rejected at COMPILE TIME!
    ASSERT_THROWS(compile_wat(R"(
        (module
          (func (export "bad") (param $s ro_handle)
            (mem.write_u8 (local.get $s) (i32.const 0) (i32.const 65))))
    )"), CompileError);

    // Spanning an ro_handle preserves ro_handle, so writing to the span is also rejected at compile time!
    ASSERT_THROWS(compile_wat(R"(
        (module
          (func (export "bad_span") (param $s ro_handle)
            (mem.write_u8 (mem.span (local.get $s) (i32.const 0) (i32.const 4)) (i32.const 0) (i32.const 65))))
    )"), CompileError);
}

TEST(split_block_linked_list_with_refs) {
    // Each node is alloc(4 bytes for value, 1 ref for next)
    const char* wat = R"(
    (module
      (func $make_node (param $val i32) (param $next rw_handle) (result rw_handle)
        (local $n rw_handle)
        (local.set $n (mem.alloc (i32.const 4) (i32.const 1)))
        (mem.write_i32 (local.get $n) (i32.const 0) (local.get $val))
        (mem.write_ref (local.get $n) (i32.const 0) (local.get $next))
        (local.get $n))

      (func (export "sum_list") (param $count i32) (result i32)
        (local $head rw_handle)
        (local $i i32)
        (local $sum i32)
        ;; Build list 1..count
        (local.set $i (i32.const 1))
        (block $build_done
          (loop $build
            (br_if $build_done (i32.gt_s (local.get $i) (local.get $count)))
            (local.set $head (call $make_node (local.get $i) (local.get $head)))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br $build)))
        ;; Traverse list and sum values
        (block $walk_done
          (loop $walk
            (br_if $walk_done (mem.is_null (local.get $head)))
            (local.set $sum (i32.add (local.get $sum) (mem.read_i32 (local.get $head) (i32.const 0))))
            (local.set $head (mem.read_ref_rw (local.get $head) (i32.const 0)))
            (br $walk)))
        (local.get $sum))
    )
    )";
    CompiledModule mod = compile_wat(wat);
    MemoryStore store;
    ModuleInstance inst(std::move(mod), store);
    VM vm;
    ASSERT_EQ(vm.invoke(inst, "sum_list", {Value::from_i32(10)})[0].i32, 55);
}

TEST(bulk_mem_copy_fill_cmp) {
    const char* wat = R"(
    (module
      (func (export "bulk") (result i32)
        (local $a rw_handle)
        (local $b rw_handle)
        (local.set $a (mem.alloc (i32.const 16) (i32.const 0)))
        (local.set $b (mem.alloc (i32.const 16) (i32.const 0)))

        ;; Fill a[0..8) with 0x2A, a[8..16) with 0x55
        (mem.fill (local.get $a) (i32.const 0) (i32.const 42) (i32.const 8))
        (mem.fill (local.get $a) (i32.const 8) (i32.const 85) (i32.const 8))

        ;; Copy a[0..16) into b[0..16)
        (mem.copy (local.get $b) (i32.const 0) (local.get $a) (i32.const 0) (i32.const 16))
        (if (i32.ne (mem.cmp (local.get $a) (i32.const 0) (local.get $b) (i32.const 0) (i32.const 16)) (i32.const 0))
          (then (return (i32.const -1))))

        ;; Overlapping copy within b: b[4..12) <- b[0..8)
        (mem.copy (local.get $b) (i32.const 4) (local.get $b) (i32.const 0) (i32.const 8))
        ;; Now b[0..12) are all 42, while a[8] is 85 => cmp(a, b) > 0
        (mem.cmp (local.get $a) (i32.const 0) (local.get $b) (i32.const 0) (i32.const 16)))
    )
    )";
    CompiledModule mod = compile_wat(wat);
    MemoryStore store;
    ModuleInstance inst(std::move(mod), store);
    VM vm;
    ASSERT_EQ(vm.invoke(inst, "bulk")[0].i32, 1);
}

TEST(mem_copy_refs_and_readonly_noop) {
    const char* wat = R"(
    (module
      (string $s0 "zero")
      (string $s1 "one")
      (string $s2 "two")
      (func (export "test") (result i32)
        (local $a rw_handle)
        (local $b rw_handle)
        (local $ro1 ro_handle)
        (local $ro2 ro_handle)

        ;; as_readonly on an already-readonly handle must be a zero-alloc no-op (same handle ID)
        (local.set $ro1 (mem.const_str $s0))
        (local.set $ro2 (mem.readonly (local.get $ro1)))
        (if (i32.eqz (mem.eq (local.get $ro1) (local.get $ro2)))
          (then (return (i32.const -1))))

        ;; mem.copy_refs between two ref arrays + overlapping shift within $b
        (local.set $a (mem.alloc (i32.const 0) (i32.const 3)))
        (local.set $b (mem.alloc (i32.const 0) (i32.const 4)))
        (mem.write_ref (local.get $a) (i32.const 0) (mem.const_str $s0))
        (mem.write_ref (local.get $a) (i32.const 1) (mem.const_str $s1))
        (mem.write_ref (local.get $a) (i32.const 2) (mem.const_str $s2))

        (mem.copy_refs (local.get $b) (i32.const 0) (local.get $a) (i32.const 0) (i32.const 3))
        ;; Overlapping shift: b[1..4) <- b[0..3)
        (mem.copy_refs (local.get $b) (i32.const 1) (local.get $b) (i32.const 0) (i32.const 3))

        ;; Expect b[0]==s0, b[1]==s0, b[2]==s1, b[3]==s2
        (i32.add
          (i32.add
            (mem.eq (mem.read_ref_ro (local.get $b) (i32.const 0)) (mem.const_str $s0))
            (mem.eq (mem.read_ref_ro (local.get $b) (i32.const 1)) (mem.const_str $s0)))
          (i32.add
            (mem.eq (mem.read_ref_ro (local.get $b) (i32.const 2)) (mem.const_str $s1))
            (mem.eq (mem.read_ref_ro (local.get $b) (i32.const 3)) (mem.const_str $s2)))))
    )
    )";
    CompiledModule mod = compile_wat(wat);
    MemoryStore store;
    ModuleInstance inst(std::move(mod), store);
    VM vm;
    size_t live_before = store.live_block_count();
    ASSERT_EQ(vm.invoke(inst, "test")[0].i32, 4);
    // Only $a and $b were allocated (zero extra blocks from mem.readonly on $ro1)
    ASSERT_EQ(store.live_block_count() - live_before, 2u);
}

TEST(mem_sub_block_inline_struct_views) {
    const char* wat = R"(
    (module
      (string $s_tag "inline_ok")
      (func (export "test") (result i32)
        (local $outer rw_handle)
        (local $sub rw_handle)
        (local $ro_sub ro_handle)
        ;; Outer: bytes[0..4)=id, bytes[4..12)=inline Inner{x,y}, refs[0]=outer_str, refs[1]=inline Inner.label
        (local.set $outer (mem.alloc (i32.const 12) (i32.const 2)))
        (mem.write_i32 (local.get $outer) (i32.const 0) (i32.const 99))
        (mem.write_i32 (local.get $outer) (i32.const 4) (i32.const 10))
        (mem.write_i32 (local.get $outer) (i32.const 8) (i32.const 20))
        (mem.write_ref (local.get $outer) (i32.const 1) (mem.const_str $s_tag))

        ;; Take zero-copy sub_block view of Inner: bytes[4..12) + refs[1..2)
        (local.set $sub (mem.sub_block (local.get $outer) (i32.const 4) (i32.const 8) (i32.const 1) (i32.const 1)))
        ;; Mutate through sub_block -> aliases outer!
        (mem.write_i32 (local.get $sub) (i32.const 0) (i32.const 30))
        ;; Verify outer bytes[4..8) == 30, sub ref_len == 1, byte_len == 8, and ro_sub reads match
        (local.set $ro_sub (mem.sub_block (mem.readonly (local.get $outer)) (i32.const 4) (i32.const 8) (i32.const 1) (i32.const 1)))
        (if (i32.ne (mem.read_i32 (local.get $ro_sub) (i32.const 0)) (i32.const 30)) (then (return (i32.const -1))))
        (if (i32.ne (mem.byte_len (local.get $sub)) (i32.const 8)) (then (return (i32.const -2))))
        (if (i32.ne (mem.ref_len (local.get $sub)) (i32.const 1)) (then (return (i32.const -3))))
        (if (i32.eqz (mem.eq (mem.read_ref_ro (local.get $sub) (i32.const 0)) (mem.const_str $s_tag)))
          (then (return (i32.const -4))))
        ;; 30 + 20 + 99 = 149
        (i32.add (mem.read_i32 (local.get $outer) (i32.const 4))
                 (i32.add (mem.read_i32 (local.get $sub) (i32.const 4))
                          (mem.read_i32 (local.get $outer) (i32.const 0)))))
    )
    )";
    CompiledModule mod = compile_wat(wat);
    MemoryStore store;
    ModuleInstance inst(std::move(mod), store);
    VM vm;
    ASSERT_EQ(vm.invoke(inst, "test")[0].i32, 149);
}

int main() {
    return optrich_test::run_all_tests();
}
