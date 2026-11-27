;; Guessed WAT for examples/optic/04b_tuples_result_and_defer.opc
;; Tuple (i32, i32) -> multi-value (result i32 i32)
;; Result<i32, i32> -> multi-value (result i32 i32) where first i32 is is_err (0=Ok, 1=Err)
(module
  (import "host" "trace_i32" (func $trace_i32 (param ro_handle i32)))

  (string $l1 "step1.divmod_q")
  (string $l2 "step2.divmod_r")
  (string $l3 "step3.ok_res")
  (string $l4 "step4.err_code")
  (string $d_outer "defer.outer")
  (string $d_inner "defer.inner")

  (func $divmod (param $a i32) (param $b i32) (result i32 i32)
    (i32.div_s (local.get $a) (local.get $b))
    (i32.rem_s (local.get $a) (local.get $b)))

  (func $checked_div (param $a i32) (param $b i32) (result i32 i32)
    (if (i32.eqz (local.get $b))
      (then (return (i32.const 1) (i32.const 100))))
    (i32.const 0)
    (i32.div_s (local.get $a) (local.get $b)))

  ;; `defer` lowered via LIFO cleanup epilogue shared across both Ok and `?` Err exit paths
  (func $compute_with_defer (param $a i32) (param $b i32) (result i32 i32)
    (local $is_err i32)
    (local $val i32)
    (local $ret_err i32)
    (local $ret_val i32)

    (block $cleanup
      ;; let q = checked_div(a, b)?;
      (call $checked_div (local.get $a) (local.get $b))
      (local.set $val)
      (local.set $is_err)
      (if (local.get $is_err)
        (then
          (local.set $ret_err (i32.const 1))
          (local.set $ret_val (local.get $val))
          (br $cleanup)))

      ;; Normal return: Ok(q * 10)
      (local.set $ret_err (i32.const 0))
      (local.set $ret_val (i32.mul (local.get $val) (i32.const 10))))

    ;; LIFO defer epilogue (inner first, then outer)
    (call $trace_i32 (mem.const_str $d_inner) (i32.const 2))
    (call $trace_i32 (mem.const_str $d_outer) (i32.const 1))
    (local.get $ret_err)
    (local.get $ret_val))

  (func (export "main") (result i32)
    (local $q i32)
    (local $r i32)
    (local $is_err i32)
    (local $payload i32)
    (local $ok_val i32)
    (local $err_code i32)

    ;; let (q, r) = divmod(23, 5);
    (call $divmod (i32.const 23) (i32.const 5))
    (local.set $r)
    (local.set $q)
    (call $trace_i32 (mem.const_str $l1) (local.get $q))
    (call $trace_i32 (mem.const_str $l2) (local.get $r))

    ;; Ok path
    (call $compute_with_defer (i32.const 20) (i32.const 4))
    (local.set $payload)
    (local.set $is_err)
    (local.set $ok_val
      (if (result i32) (i32.eqz (local.get $is_err))
        (then (local.get $payload))
        (else (i32.const -1))))
    (call $trace_i32 (mem.const_str $l3) (local.get $ok_val))

    ;; Err path via `?`
    (call $compute_with_defer (i32.const 20) (i32.const 0))
    (local.set $payload)
    (local.set $is_err)
    (local.set $err_code
      (if (result i32) (local.get $is_err)
        (then (local.get $payload))
        (else (i32.const -1))))
    (call $trace_i32 (mem.const_str $l4) (local.get $err_code))

    (i32.add (i32.add (local.get $q) (local.get $r))
             (i32.add (local.get $ok_val) (local.get $err_code))))
)
