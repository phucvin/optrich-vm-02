#include "optrich/compiler.hpp"
#include "optrich/memory_store.hpp"
#include "optrich/vm.hpp"
#include "test_runner.hpp"

using namespace optrich;

static std::vector<Value> run_wat(std::string_view wat, std::string_view fn,
                                  const std::vector<Value>& args = {}) {
    CompiledModule mod = compile_wat(wat);
    MemoryStore store;
    ModuleInstance inst(std::move(mod), store);
    VM vm;
    return vm.invoke(inst, fn, args);
}

TEST(i32_arithmetic_and_bitwise) {
    const char* wat = R"(
    (module
      (func (export "calc") (param $a i32) (param $b i32) (result i32)
        (i32.add
          (i32.mul (local.get $a) (local.get $b))
          (i32.sub (local.get $a) (local.get $b))))
      (func (export "bits") (param $x i32) (result i32)
        (i32.add
          (i32.popcnt (local.get $x))
          (i32.add (i32.clz (local.get $x)) (i32.ctz (local.get $x)))))
      (func (export "rot") (param $x i32) (result i32)
        (i32.rotr (i32.rotl (local.get $x) (i32.const 7)) (i32.const 7)))
      (func (export "div_zero") (result i32)
        (i32.div_s (i32.const 10) (i32.const 0)))
    )
    )";
    auto r1 = run_wat(wat, "calc", {Value::from_i32(7), Value::from_i32(3)});
    ASSERT_EQ(r1[0].i32, 7 * 3 + (7 - 3));

    // x = 8 (0b1000): popcnt=1, clz=28, ctz=3 => 32
    auto r2 = run_wat(wat, "bits", {Value::from_i32(8)});
    ASSERT_EQ(r2[0].i32, 32);

    auto r3 = run_wat(wat, "rot", {Value::from_i32(0x12345678)});
    ASSERT_EQ(r3[0].i32, 0x12345678);

    ASSERT_THROWS(run_wat(wat, "div_zero"), Trap);
}

TEST(i64_f32_f64_and_conversions) {
    const char* wat = R"(
    (module
      (func (export "i64_ops") (param $x i64) (result i64)
        (i64.add (i64.mul (local.get $x) (i64.const 1000000)) (i64.const 42)))
      (func (export "f64_ops") (param $x f64) (result f64)
        (f64.sqrt (f64.add (f64.mul (local.get $x) (local.get $x)) (f64.const 16.0))))
      (func (export "f32_nearest") (param $x f32) (result f32)
        (f32.nearest (local.get $x)))
      (func (export "conv") (param $x i64) (result i32)
        (i32.trunc_f64_s (f64.promote_f32 (f32.convert_i32_s (i32.wrap_i64 (local.get $x))))))
    )
    )";
    auto r1 = run_wat(wat, "i64_ops", {Value::from_i64(5000)});
    ASSERT_EQ(r1[0].i64, 5000000042LL);

    auto r2 = run_wat(wat, "f64_ops", {Value::from_f64(3.0)});
    ASSERT_NEAR(r2[0].f64, 5.0, 1e-12);

    // Ties-to-even: 2.5 -> 2.0, 3.5 -> 4.0
    ASSERT_NEAR(run_wat(wat, "f32_nearest", {Value::from_f32(2.5f)})[0].f32, 2.0f, 1e-6f);
    ASSERT_NEAR(run_wat(wat, "f32_nearest", {Value::from_f32(3.5f)})[0].f32, 4.0f, 1e-6f);

    auto r4 = run_wat(wat, "conv", {Value::from_i64(-123)});
    ASSERT_EQ(r4[0].i32, -123);
}

TEST(control_flow_if_else_loop_return_br_table) {
    const char* wat = R"(
    (module
      ;; Iterative factorial using unfolded loop + br_if + early return
      (func (export "fact") (param $n i32) (result i32)
        (local $acc i32)
        (if (i32.le_s (local.get $n) (i32.const 1))
          (then (return (i32.const 1))))
        i32.const 1
        local.set $acc
        block $done
          loop $again
            local.get $n
            i32.const 1
            i32.le_s
            br_if $done
            local.get $acc
            local.get $n
            i32.mul
            local.set $acc
            local.get $n
            i32.const 1
            i32.sub
            local.set $n
            br $again
          end
        end
        local.get $acc)

      ;; Recursive fib
      (func $fib (export "fib") (param $n i32) (result i32)
        (if (result i32) (i32.lt_s (local.get $n) (i32.const 2))
          (then (local.get $n))
          (else
            (i32.add
              (call $fib (i32.sub (local.get $n) (i32.const 1)))
              (call $fib (i32.sub (local.get $n) (i32.const 2)))))))

      ;; br_table switch
      (func (export "switch") (param $x i32) (result i32)
        (block $d
          (block $b2
            (block $b1
              (block $b0
                (br_table $b0 $b1 $b2 $d (local.get $x)))
              (return (i32.const 10)))
            (return (i32.const 20)))
          (return (i32.const 30)))
        (i32.const 99))
    )
    )";
    ASSERT_EQ(run_wat(wat, "fact", {Value::from_i32(0)})[0].i32, 1);
    ASSERT_EQ(run_wat(wat, "fact", {Value::from_i32(6)})[0].i32, 720);
    ASSERT_EQ(run_wat(wat, "fib", {Value::from_i32(10)})[0].i32, 55);
    ASSERT_EQ(run_wat(wat, "switch", {Value::from_i32(0)})[0].i32, 10);
    ASSERT_EQ(run_wat(wat, "switch", {Value::from_i32(1)})[0].i32, 20);
    ASSERT_EQ(run_wat(wat, "switch", {Value::from_i32(2)})[0].i32, 30);
    ASSERT_EQ(run_wat(wat, "switch", {Value::from_i32(5)})[0].i32, 99);
}

TEST(typed_local_zero_initialization_and_globals) {
    const char* wat = R"(
    (module
      (global $counter (mut i32) (i32.const 10))
      (func (export "check_zeros") (result i32)
        (local $a i32)
        (local $b i64)
        (local $c f64)
        (local $h ro_handle)
        (if (i32.ne (local.get $a) (i32.const 0)) (then (return (i32.const 0))))
        (if (i64.ne (local.get $b) (i64.const 0)) (then (return (i32.const 0))))
        (if (f64.ne (local.get $c) (f64.const 0.0)) (then (return (i32.const 0))))
        (if (i32.eqz (mem.is_null (local.get $h))) (then (return (i32.const 0))))
        (global.set $counter (i32.add (global.get $counter) (i32.const 5)))
        (global.get $counter))
    )
    )";
    ASSERT_EQ(run_wat(wat, "check_zeros")[0].i32, 15);
}

TEST(fail_fast_validation_errors) {
    // 1. Unknown opcode
    ASSERT_THROWS(compile_wat(R"(
        (module (func (export "f") (i32.bogus_op)))
    )"), CompileError);

    // 2. Opaque handle cannot be forged from or used as i32
    ASSERT_THROWS(compile_wat(R"(
        (module (func (export "f") (result i32)
          (i32.add (mem.alloc (i32.const 8) (i32.const 0)) (i32.const 1))))
    )"), CompileError);

    ASSERT_THROWS(compile_wat(R"(
        (module (func (export "f") (result i32)
          (mem.read_i32 (i32.const 1) (i32.const 0))))
    )"), CompileError);

    // 3. Strict lexical block scoping: label out of scope
    ASSERT_THROWS(compile_wat(R"(
        (module (func (export "f")
          (block $inner (nop))
          (br $inner)))
    )"), CompileError);

    // 4. Unclosed block
    ASSERT_THROWS(compile_wat(R"(
        (module (func (export "f")
          block
            nop))
    )"), CompileError);
}

int main() {
    return optrich_test::run_all_tests();
}
