#include "optrich/runtime.hpp"
#include "test_runner.hpp"

using namespace optrich;

TEST(direct_cross_module_zero_copy_readonly_sharing) {
    Runtime rt;

    // Module `strlib` operates on read-only UTF-8 spans with zero copying
    rt.instantiate_wat("strlib", R"(
    (module
      (func (export "count_char") (param $s ro_handle) (param $ch i32) (result i32)
        (local $i i32)
        (local $n i32)
        (local $cnt i32)
        (local.set $n (mem.byte_len (local.get $s)))
        (block $done
          (loop $lp
            (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
            (if (i32.eq (mem.read_u8 (local.get $s) (local.get $i)) (local.get $ch))
              (then (local.set $cnt (i32.add (local.get $cnt) (i32.const 1)))))
            (local.set $i (i32.add (local.get $i) (i32.const 1)))
            (br $lp)))
        (local.get $cnt))

      ;; Malicious consumer attempting to mutate a dynamic handle
      (func (export "try_mutate") (param $h handle)
        (mem.write_u8 (local.get $h) (i32.const 0) (i32.const 88)))
    )
    )");

    // Module `app` imports directly from `strlib` (direct CallFrame push, no boxing)
    rt.instantiate_wat("app", R"(
    (module
      (import "strlib" "count_char" (func $count_char (param ro_handle i32) (result i32)))
      (import "strlib" "try_mutate" (func $try_mutate (param handle)))
      (string $msg "banana_bandana")

      (func (export "run_slice_count") (result i32)
        (local $sub ro_handle)
        ;; Zero-copy slice "banana" (offset 0, len 6) -> has three 'a' (97)
        (local.set $sub (mem.span (mem.const_str $msg) (i32.const 0) (i32.const 6)))
        (call $count_char (local.get $sub) (i32.const 97)))

      (func (export "run_ro_protection")
        (local $buf rw_handle)
        (local.set $buf (mem.alloc (i32.const 4) (i32.const 0)))
        ;; Downgrade to read-only before passing across module boundary
        (call $try_mutate (mem.readonly (local.get $buf))))
    )
    )");

    ASSERT_EQ(rt.invoke("app", "run_slice_count")[0].i32, 3);
    ASSERT_THROWS(rt.invoke("app", "run_ro_protection"), Trap);
}

TEST(cross_module_tables_and_polymorphic_interface_handles) {
    // Demonstrates the shared convention for polymorphic interface handles:
    // - Shared vtable `iface.vtable` holds cross-module FuncRefs
    // - Each object is a split-block where byte[0..4) = method table slot, byte[4..8) = state
    Runtime rt;

    // 1. `iface` owns the shared dispatch table and provides polymorphic `invoke_eval`
    rt.instantiate_wat("iface", R"(
    (module
      (type $eval_sig (func (param handle i32) (result i32)))
      (table $vtable (export "vtable") 8 funcref)

      (func (export "eval") (param $obj handle) (param $x i32) (result i32)
        (local $slot i32)
        (local.set $slot (mem.read_i32 (local.get $obj) (i32.const 0)))
        (call_indirect (type $eval_sig) (local.get $obj) (local.get $x) (local.get $slot)))
    )
    )");

    // 2. `adder` registers its method at table slot 0 (returns state + x)
    rt.instantiate_wat("adder", R"(
    (module
      (import "iface" "vtable" (table $vtable 8 funcref))
      (global $bias i32 (i32.const 100))

      (func $add_eval (param $self handle) (param $x i32) (result i32)
        ;; Uses `adder`'s own global `$bias` to prove target ModuleInstance switched!
        (i32.add
          (global.get $bias)
          (i32.add (mem.read_i32 (local.get $self) (i32.const 4)) (local.get $x))))

      (elem (i32.const 0) $add_eval)

      (func (export "new_adder") (param $k i32) (result handle)
        (local $o rw_handle)
        (local.set $o (mem.alloc (i32.const 8) (i32.const 0)))
        (mem.write_i32 (local.get $o) (i32.const 0) (i32.const 0)) ;; vtable slot 0
        (mem.write_i32 (local.get $o) (i32.const 4) (local.get $k))
        (local.get $o))
    )
    )");

    // 3. `multiplier` registers its method at table slot 1 (returns state * x)
    rt.instantiate_wat("multiplier", R"(
    (module
      (import "iface" "vtable" (table $vtable 8 funcref))

      (func $mul_eval (param $self handle) (param $x i32) (result i32)
        (i32.mul (mem.read_i32 (local.get $self) (i32.const 4)) (local.get $x)))

      (elem (i32.const 1) $mul_eval)

      (func (export "new_mul") (param $factor i32) (result handle)
        (local $o rw_handle)
        (local.set $o (mem.alloc (i32.const 8) (i32.const 0)))
        (mem.write_i32 (local.get $o) (i32.const 0) (i32.const 1)) ;; vtable slot 1
        (mem.write_i32 (local.get $o) (i32.const 4) (local.get $factor))
        (local.get $o))
    )
    )");

    // 4. `main` creates both objects and dispatches polymorphically through `iface.eval`
    rt.instantiate_wat("main", R"(
    (module
      (import "iface" "eval" (func $eval (param handle i32) (result i32)))
      (import "adder" "new_adder" (func $new_adder (param i32) (result handle)))
      (import "multiplier" "new_mul" (func $new_mul (param i32) (result handle)))

      (func (export "run") (result i32)
        (local $a handle)
        (local $m handle)
        (local.set $a (call $new_adder (i32.const 5)))
        (local.set $m (call $new_mul (i32.const 4)))
        ;; adder(5, 7) = 100 + 5 + 7 = 112; mul(4, 7) = 28 => 140
        (i32.add
          (call $eval (local.get $a) (i32.const 7))
          (call $eval (local.get $m) (i32.const 7))))
    )
    )");

    ASSERT_EQ(rt.invoke("main", "run")[0].i32, 140);
}

TEST(host_func_interop_and_link_errors) {
    Runtime rt;
    rt.register_host_func(
        "host", "make_greeting",
        FuncSignature{{}, {ValType::RoHandle}},
        [](MemoryStore& store, const std::vector<Value>&) -> std::vector<Value> {
            return {Value::from_handle(store.alloc_readonly_bytes("hi"))};
        });

    rt.instantiate_wat("client", R"(
    (module
      (import "host" "make_greeting" (func $make_greeting (result ro_handle)))
      (func (export "len") (result i32)
        (mem.byte_len (call $make_greeting)))
    )
    )");
    ASSERT_EQ(rt.invoke("client", "len")[0].i32, 2);

    // Missing import fails with LinkError
    ASSERT_THROWS(rt.instantiate_wat("bad1", R"(
        (module (import "nope" "fn" (func $f)))
    )"), LinkError);

    // Signature mismatch fails with LinkError
    ASSERT_THROWS(rt.instantiate_wat("bad2", R"(
        (module (import "client" "len" (func $f (param i32) (result i32))))
    )"), LinkError);
}

TEST(dynamic_funcref_table_grow_start_and_multi_interface_trait_objects) {
    Runtime rt;

    // Interface A: private vtable grown dynamically via `table.grow` + `ref.func`
    rt.instantiate_wat("iface_a", R"(
    (module
      (type $fn_t (func (param handle) (result i32)))
      (table $t 0 funcref)
      (func (export "register") (param $f funcref) (result i32)
        (table.grow $t (local.get $f) (i32.const 1)))
      (func (export "size") (result i32)
        (table.size $t))
      (func (export "wrap") (param $d handle) (param $slot i32) (result ro_handle)
        (local $w rw_handle)
        (local.set $w (mem.alloc (i32.const 4) (i32.const 1)))
        (mem.write_i32 (local.get $w) (i32.const 0) (local.get $slot))
        (mem.write_ref (local.get $w) (i32.const 0) (local.get $d))
        (mem.readonly (local.get $w)))
      (func (export "call_a") (param $obj ro_handle) (result i32)
        (call_indirect $t (type $fn_t)
          (mem.read_ref (local.get $obj) (i32.const 0))
          (mem.read_i32 (local.get $obj) (i32.const 0))))
    )
    )");

    // Interface B: completely independent private vtable
    rt.instantiate_wat("iface_b", R"(
    (module
      (type $fn_t (func (param handle) (result i32)))
      (table $t 0 funcref)
      (func (export "register") (param $f funcref) (result i32)
        (table.grow $t (local.get $f) (i32.const 1)))
      (func (export "wrap") (param $d handle) (param $slot i32) (result ro_handle)
        (local $w rw_handle)
        (local.set $w (mem.alloc (i32.const 4) (i32.const 1)))
        (mem.write_i32 (local.get $w) (i32.const 0) (local.get $slot))
        (mem.write_ref (local.get $w) (i32.const 0) (local.get $d))
        (mem.readonly (local.get $w)))
      (func (export "call_b") (param $obj ro_handle) (result i32)
        (call_indirect $t (type $fn_t)
          (mem.read_ref (local.get $obj) (i32.const 0))
          (mem.read_i32 (local.get $obj) (i32.const 0))))
    )
    )");

    // Implementer registers into BOTH interfaces automatically via `(start $init)`
    rt.instantiate_wat("box", R"(
    (module
      (import "iface_a" "register" (func $reg_a (param funcref) (result i32)))
      (import "iface_a" "wrap"     (func $wrap_a (param handle i32) (result ro_handle)))
      (import "iface_b" "register" (func $reg_b (param funcref) (result i32)))
      (import "iface_b" "wrap"     (func $wrap_b (param handle i32) (result ro_handle)))

      (global $slot_a (mut i32) (i32.const -1))
      (global $slot_b (mut i32) (i32.const -1))

      (func $impl_a (param $self handle) (result i32)
        (i32.add (mem.read_i32 (local.get $self) (i32.const 0)) (i32.const 10)))
      (func $impl_b (param $self handle) (result i32)
        (i32.mul (mem.read_i32 (local.get $self) (i32.const 0)) (i32.const 3)))

      (func $init
        (global.set $slot_a (call $reg_a (ref.func $impl_a)))
        (global.set $slot_b (call $reg_b (ref.func $impl_b))))
      (start $init)

      (func (export "make_and_test") (param $v i32) (result i32)
        (local $d rw_handle)
        (local.set $d (mem.alloc (i32.const 4) (i32.const 0)))
        (mem.write_i32 (local.get $d) (i32.const 0) (local.get $v))
        ;; Return slot_a * 1000 + impl_a(v) + impl_b(v) via wrappers
        (i32.add
          (i32.mul (global.get $slot_a) (i32.const 1000))
          (i32.add
            (call $impl_a (local.get $d))
            (call $impl_b (local.get $d)))))
      (func (export "as_a") (param $d handle) (result ro_handle)
        (call $wrap_a (local.get $d) (global.get $slot_a)))
      (func (export "as_b") (param $d handle) (result ro_handle)
        (call $wrap_b (local.get $d) (global.get $slot_b)))
      (func (export "new_box") (param $v i32) (result handle)
        (local $d rw_handle)
        (local.set $d (mem.alloc (i32.const 4) (i32.const 0)))
        (mem.write_i32 (local.get $d) (i32.const 0) (local.get $v))
        (local.get $d))
    )
    )");

    // Caller invokes both interfaces on the exact same `box(7)` handle
    rt.instantiate_wat("driver", R"(
    (module
      (import "iface_a" "call_a" (func $call_a (param ro_handle) (result i32)))
      (import "iface_b" "call_b" (func $call_b (param ro_handle) (result i32)))
      (import "box" "new_box" (func $new_box (param i32) (result handle)))
      (import "box" "as_a"    (func $as_a    (param handle) (result ro_handle)))
      (import "box" "as_b"    (func $as_b    (param handle) (result ro_handle)))

      (func (export "run") (result i32)
        (local $b handle)
        (local.set $b (call $new_box (i32.const 7)))
        ;; call_a(7) = 17, call_b(7) = 21 => 38
        (i32.add
          (call $call_a (call $as_a (local.get $b)))
          (call $call_b (call $as_b (local.get $b)))))
    )
    )");

    ASSERT_EQ(rt.invoke("iface_a", "size")[0].i32, 1);
    ASSERT_EQ(rt.invoke("driver", "run")[0].i32, 38);
}

int main() {
    return optrich_test::run_all_tests();
}
