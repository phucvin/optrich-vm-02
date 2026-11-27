;; Guessed WAT for examples/optic/01_basics.opc
;; Follows docs/optic-spec.md v0.1 lowering rules (including superinstruction-friendly loops).
(module
  (import "host" "trace_i32" (func $trace_i32 (param ro_handle i32)))
  (import "host" "trace_i64" (func $trace_i64 (param ro_handle i64)))
  (import "host" "trace_f64" (func $trace_f64 (param ro_handle f64)))

  (string $s0 "step1.fib10")
  (string $s1 "step2.sum100")
  (string $s2 "step3.collatz27")
  (string $s3 "step4.big_square")
  (string $s4 "step5.avg")

  (func $fib (param $n i32) (result i32)
    (if (i32.lt_s (local.get $n) (i32.const 2))
      (then
        (return (local.get $n))))
    (i32.add
      (call $fib (i32.sub (local.get $n) (i32.const 1)))
      (call $fib (i32.sub (local.get $n) (i32.const 2)))))

  (func $sum_range (param $n i32) (result i32)
    (local $acc i32)
    (local $i i32)
    (local $end i32)
    (local.set $acc (i32.const 0))
    (local.set $i (i32.const 1))
    (local.set $end (i32.add (local.get $n) (i32.const 1)))
    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $end)))
        (local.set $acc (i32.add (local.get $acc) (local.get $i)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))
    (local.get $acc))

  (func $collatz_steps (param $start i32) (result i32)
    (local $n i32)
    (local $steps i32)
    (local.set $n (local.get $start))
    (local.set $steps (i32.const 0))
    (block $break
      (loop $cont
        (br_if $break (i32.eq (local.get $n) (i32.const 1)))
        (if (i32.eqz (i32.rem_s (local.get $n) (i32.const 2)))
          (then
            (local.set $n (i32.div_s (local.get $n) (i32.const 2))))
          (else
            (local.set $n (i32.add (i32.mul (local.get $n) (i32.const 3)) (i32.const 1)))))
        (local.set $steps (i32.add (local.get $steps) (i32.const 1)))
        (br $cont)))
    (local.get $steps))

  (func $square_i64 (param $x i32) (result i64)
    (local $big i64)
    (local.set $big (i64.extend_i32_s (local.get $x)))
    (i64.mul (local.get $big) (local.get $big)))

  (func (export "main") (result i32)
    (local $f10 i32)
    (local $s100 i32)
    (local $c27 i32)
    (local $sq i64)
    (local $avg f64)

    (local.set $f10 (call $fib (i32.const 10)))
    (call $trace_i32 (mem.const_str $s0) (local.get $f10))

    (local.set $s100 (call $sum_range (i32.const 100)))
    (call $trace_i32 (mem.const_str $s1) (local.get $s100))

    (local.set $c27 (call $collatz_steps (i32.const 27)))
    (call $trace_i32 (mem.const_str $s2) (local.get $c27))

    (local.set $sq (call $square_i64 (i32.const 1000000)))
    (call $trace_i64 (mem.const_str $s3) (local.get $sq))

    (local.set $avg (f64.div (f64.convert_i32_s (local.get $s100)) (f64.const 100.0)))
    (call $trace_f64 (mem.const_str $s4) (local.get $avg))

    (i32.add (i32.add (local.get $f10) (local.get $s100)) (local.get $c27)))
)
