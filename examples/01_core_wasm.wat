;; Example 01 — Core Wasm Semantics (Phase 1)
;; Showcases: i32/i64/f32/f64 arithmetic, bitwise ops, conversions,
;;            if/else, block/loop, br_if, br_table, recursion, and globals.
;;
;; Run: ./build/optrich-vm examples/01_core_wasm.wat main
;;      ./build/optrich-vm examples/01_core_wasm.wat fib 12
;;      ./build/optrich-vm examples/01_core_wasm.wat dispatch 2
(module
  (global $calls (mut i32) (i32.const 0))

  ;; Recursive Fibonacci (demonstrates call stack frames & if/else with result)
  (func $fib (export "fib") (param $n i32) (result i32)
    (global.set $calls (i32.add (global.get $calls) (i32.const 1)))
    (if (result i32) (i32.lt_s (local.get $n) (i32.const 2))
      (then (local.get $n))
      (else
        (i32.add
          (call $fib (i32.sub (local.get $n) (i32.const 1)))
          (call $fib (i32.sub (local.get $n) (i32.const 2)))))))

  ;; br_table switch dispatch
  (func $dispatch (export "dispatch") (param $op i32) (result i32)
    (block $default
      (block $case2
        (block $case1
          (block $case0
            (br_table $case0 $case1 $case2 $default (local.get $op)))
          (return (i32.const 100)))
        (return (i32.const 200)))
      (return (i32.const 300)))
    (i32.const -1))

  ;; Floating-point & conversion pipeline: sqrt(3^2 + 4^2) = 5 -> i32
  (func $hypotenuse (param $a f64) (param $b f64) (result i32)
    (i32.trunc_f64_s
      (f64.sqrt
        (f64.add
          (f64.mul (local.get $a) (local.get $a))
          (f64.mul (local.get $b) (local.get $b))))))

  ;; Entry point: fib(10)=55 + dispatch(1)=200 + hypotenuse(3,4)=5 + popcnt(0xF0)=4 => 264
  (func (export "main") (result i32)
    (i32.add
      (i32.add (call $fib (i32.const 10)) (call $dispatch (i32.const 1)))
      (i32.add (call $hypotenuse (f64.const 3.0) (f64.const 4.0))
               (i32.popcnt (i32.const 0xF0)))))
)
