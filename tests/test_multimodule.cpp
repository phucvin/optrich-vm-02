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

int main() {
    return optrich_test::run_all_tests();
}
