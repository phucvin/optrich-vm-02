;; Guessed WAT for examples/optic/01b_control_and_bits.opc
(module
  (import "host" "trace_i32" (func $trace_i32 (param ro_handle i32)))

  (string $l1 "step1.for_break_continue")
  (string $l2 "step2.short_circuit_hits")
  (string $l3 "step3.short_circuit_bits")
  (string $l4 "step4.classify")
  (string $l5 "step5.unsigned_bits")

  ;; Counter: bytes[0..4) = hits (i32) -> mem.alloc(4, 0)
  (func $Counter__bump_and_check (param $self rw_handle) (param $cond i32) (result i32)
    (mem.write_i32 (local.get $self) (i32.const 0)
      (i32.add (mem.read_i32 (local.get $self) (i32.const 0)) (i32.const 1)))
    (local.get $cond))

  (func $sum_skip_multiples_of_3_until (param $limit i32) (param $stop_sum i32) (result i32)
    (local $acc i32)
    (local $i i32)
    (local.set $acc (i32.const 0))
    (local.set $i (i32.const 1))
    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $limit)))
        (block $step
          (if (i32.eqz (i32.rem_s (local.get $i) (i32.const 3)))
            (then (br $step)))
          (local.set $acc (i32.add (local.get $acc) (local.get $i)))
          (if (i32.ge_s (local.get $acc) (local.get $stop_sum))
            (then (br $break))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))
    (local.get $acc))

  (func $classify (param $x i32) (result i32)
    (if (result i32) (i32.lt_s (local.get $x) (i32.const 0))
      (then (i32.const -1))
      (else
        (if (result i32) (i32.eqz (local.get $x))
          (then (i32.const 0))
          (else (i32.const 1))))))

  (func $unsigned_and_bits (param $v i32) (result i32)
    (local $hi i32)
    (local $masked i32)
    (local.set $hi (i32.shr_u (local.get $v) (i32.const 31)))
    (local.set $masked
      (i32.xor
        (i32.or (i32.and (local.get $v) (i32.const 255)) (i32.const 256))
        (i32.const 15)))
    (i32.add (i32.mul (local.get $hi) (i32.const 1000)) (local.get $masked)))

  (func (export "main") (result i32)
    (local $s i32)
    (local $c rw_handle)
    (local $r1 i32)
    (local $r2 i32)
    (local $r3 i32)
    (local $r4 i32)
    (local $cl i32)
    (local $b i32)

    (local.set $s (call $sum_skip_multiples_of_3_until (i32.const 50) (i32.const 25)))
    (call $trace_i32 (mem.const_str $l1) (local.get $s))

    (local.set $c (mem.alloc (i32.const 4) (i32.const 0)))
    ;; r1 = false && c.bump_and_check(true)
    (local.set $r1
      (if (result i32) (i32.const 0)
        (then (i32.ne (call $Counter__bump_and_check (local.get $c) (i32.const 1)) (i32.const 0)))
        (else (i32.const 0))))
    ;; r2 = true || c.bump_and_check(false)
    (local.set $r2
      (if (result i32) (i32.const 1)
        (then (i32.const 1))
        (else (i32.ne (call $Counter__bump_and_check (local.get $c) (i32.const 0)) (i32.const 0)))))
    ;; r3 = true && c.bump_and_check(true)
    (local.set $r3
      (if (result i32) (i32.const 1)
        (then (i32.ne (call $Counter__bump_and_check (local.get $c) (i32.const 1)) (i32.const 0)))
        (else (i32.const 0))))
    ;; r4 = false || c.bump_and_check(false)
    (local.set $r4
      (if (result i32) (i32.const 0)
        (then (i32.const 1))
        (else (i32.ne (call $Counter__bump_and_check (local.get $c) (i32.const 0)) (i32.const 0)))))

    (call $trace_i32 (mem.const_str $l2) (mem.read_i32 (local.get $c) (i32.const 0)))
    (call $trace_i32 (mem.const_str $l3)
      (i32.add
        (i32.add (i32.mul (local.get $r1) (i32.const 8)) (i32.mul (local.get $r2) (i32.const 4)))
        (i32.add (i32.mul (local.get $r3) (i32.const 2)) (local.get $r4))))

    (local.set $cl
      (i32.add
        (i32.add
          (i32.mul (call $classify (i32.const -5)) (i32.const 100))
          (i32.mul (call $classify (i32.const 0)) (i32.const 10)))
        (call $classify (i32.const 42))))
    (call $trace_i32 (mem.const_str $l4) (local.get $cl))

    ;; 0x80000005u as signed i32 = -2147483643
    (local.set $b (call $unsigned_and_bits (i32.const -2147483643)))
    (call $trace_i32 (mem.const_str $l5) (local.get $b))

    (i32.add (i32.add (local.get $s) (mem.read_i32 (local.get $c) (i32.const 0))) (local.get $b)))
)
