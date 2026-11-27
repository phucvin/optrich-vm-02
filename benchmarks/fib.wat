;; Benchmark: Recursive Fibonacci
;; Stresses: function call/return frame push/pop, branching, i32 arithmetic.
;;
;; Run: ./build/optrich-vm benchmarks/fib.wat fib 40
;; Expected: fib(40) = 102334155 (331,160,281 total calls)
(module
  (func $fib (export "fib") (param $n i32) (result i32)
    (if (result i32) (i32.lt_s (local.get $n) (i32.const 2))
      (then (local.get $n))
      (else
        (i32.add
          (call $fib (i32.sub (local.get $n) (i32.const 1)))
          (call $fib (i32.sub (local.get $n) (i32.const 2)))))))
)
