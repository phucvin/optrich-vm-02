;; Guessed WAT for examples/optic/09_closures_and_captures.opc
;; Closure env layout:
;;   add10 : mem.alloc(4, 1) -> bytes[0..4)=k, refs[0]=frozen vtable
;;   inc   : mem.alloc(0, 2) -> refs[0]=frozen vtable, refs[1]=cell_total (mem.alloc(4,0))
;;   read  : mem.alloc(0, 2) -> refs[0]=frozen vtable, refs[1]=cell_total (shared!)
(module
  (import "host" "trace_i32"  (func $trace_i32  (param ro_handle i32)))
  (import "host" "trace_meta" (func $trace_meta (param ro_handle ro_handle)))

  (type $fn_i32_i32      (func (param i32) (result i32)))
  (type $cl_i32_i32      (func (param ro_handle i32) (result i32)))
  (type $cl_void_i32     (func (param ro_handle) (result i32)))

  (global $adder_vt (mut ro_handle) (handle.null))
  (global $inc_vt   (mut ro_handle) (handle.null))
  (global $read_vt  (mut ro_handle) (handle.null))

  (string $l1 "step1.fn_double")
  (string $l2 "step2.fn_square")
  (string $l3 "step3.closure_env_meta")
  (string $l4 "step4.add10_5")
  (string $l5 "step5.mut_capture_1")
  (string $l6 "step6.mut_capture_2")
  (string $l7 "step7.mut_capture_read")

  ;; 1. Non-capturing functions
  (func $double_it (param $x i32) (result i32)
    (i32.mul (local.get $x) (i32.const 2)))

  (func $square_it (param $x i32) (result i32)
    (i32.mul (local.get $x) (local.get $x)))

  (func $apply_fn (param $f funcref) (param $x i32) (result i32)
    (call_ref (type $fn_i32_i32) (local.get $x) (local.get $f)))

  ;; 2. Capturing closure `make_adder`
  (func $adder_body (param $env ro_handle) (param $x i32) (result i32)
    (i32.add (mem.read_i32 (local.get $env) (i32.const 0)) (local.get $x)))

  (func $make_adder (param $k i32) (result ro_handle)
    (local $vt rw_handle)
    (local $cl rw_handle)
    (if (mem.is_null (global.get $adder_vt))
      (then
        (local.set $vt (mem.alloc_vtable (i32.const 1)))
        (mem.write_func (local.get $vt) (i32.const 0) (ref.func $adder_body))
        (global.set $adder_vt (mem.readonly (local.get $vt)))))
    (local.set $cl (mem.alloc (i32.const 4) (i32.const 1)))
    (mem.write_i32 (local.get $cl) (i32.const 0) (local.get $k))
    (mem.write_ref (local.get $cl) (i32.const 0) (global.get $adder_vt))
    (mem.readonly (local.get $cl)))

  ;; 3. Mutable capture closure bodies (cell at refs[1])
  (func $inc_body (param $env ro_handle) (param $delta i32) (result i32)
    (local $cell rw_handle)
    (local $next i32)
    (local.set $cell (mem.read_ref_rw (local.get $env) (i32.const 1)))
    (local.set $next (i32.add (mem.read_i32 (local.get $cell) (i32.const 0)) (local.get $delta)))
    (mem.write_i32 (local.get $cell) (i32.const 0) (local.get $next))
    (local.get $next))

  (func $read_body (param $env ro_handle) (result i32)
    (mem.read_i32 (mem.read_ref_ro (local.get $env) (i32.const 1)) (i32.const 0)))

  (func $call_cl_i32 (param $cl ro_handle) (param $x i32) (result i32)
    (call_ref (type $cl_i32_i32)
      (local.get $cl)
      (local.get $x)
      (mem.read_func (mem.read_ref_ro (local.get $cl) (i32.const 0)) (i32.const 0))))

  (func $call_cl_void (param $cl ro_handle) (result i32)
    (call_ref (type $cl_void_i32)
      (local.get $cl)
      (mem.read_func (mem.read_ref_ro (local.get $cl) (i32.const 0)) (i32.const 0))))

  (func (export "main") (result i32)
    (local $a i32)
    (local $b i32)
    (local $add10 ro_handle)
    (local $c i32)
    (local $cell_total rw_handle)
    (local $vt rw_handle)
    (local $inc rw_handle)
    (local $read rw_handle)
    (local $t1 i32)
    (local $t2 i32)
    (local $t3 i32)

    ;; 1. Non-capturing fn values
    (local.set $a (call $apply_fn (ref.func $double_it) (i32.const 7)))
    (local.set $b (call $apply_fn (ref.func $square_it) (i32.const 6)))
    (call $trace_i32 (mem.const_str $l1) (local.get $a))
    (call $trace_i32 (mem.const_str $l2) (local.get $b))

    ;; 2. Capturing closure
    (local.set $add10 (call $make_adder (i32.const 10)))
    (call $trace_meta (mem.const_str $l3) (local.get $add10))
    (local.set $c (call $call_cl_i32 (local.get $add10) (i32.const 5)))
    (call $trace_i32 (mem.const_str $l4) (local.get $c))

    ;; 3. Heap-promoted `let mut total = 100` cell
    (local.set $cell_total (mem.alloc (i32.const 4) (i32.const 0)))
    (mem.write_i32 (local.get $cell_total) (i32.const 0) (i32.const 100))

    (if (mem.is_null (global.get $inc_vt))
      (then
        (local.set $vt (mem.alloc_vtable (i32.const 1)))
        (mem.write_func (local.get $vt) (i32.const 0) (ref.func $inc_body))
        (global.set $inc_vt (mem.readonly (local.get $vt)))))
    (if (mem.is_null (global.get $read_vt))
      (then
        (local.set $vt (mem.alloc_vtable (i32.const 1)))
        (mem.write_func (local.get $vt) (i32.const 0) (ref.func $read_body))
        (global.set $read_vt (mem.readonly (local.get $vt)))))

    (local.set $inc (mem.alloc (i32.const 0) (i32.const 2)))
    (mem.write_ref (local.get $inc) (i32.const 0) (global.get $inc_vt))
    (mem.write_ref (local.get $inc) (i32.const 1) (local.get $cell_total))

    (local.set $read (mem.alloc (i32.const 0) (i32.const 2)))
    (mem.write_ref (local.get $read) (i32.const 0) (global.get $read_vt))
    (mem.write_ref (local.get $read) (i32.const 1) (local.get $cell_total))

    (local.set $t1 (call $call_cl_i32 (local.get $inc) (i32.const 15)))
    (local.set $t2 (call $call_cl_i32 (local.get $inc) (i32.const 25)))
    ;; Outer scope mutates promoted cell_total += 5
    (mem.write_i32 (local.get $cell_total) (i32.const 0)
      (i32.add (mem.read_i32 (local.get $cell_total) (i32.const 0)) (i32.const 5)))
    (local.set $t3 (call $call_cl_void (local.get $read)))

    (call $trace_i32 (mem.const_str $l5) (local.get $t1))
    (call $trace_i32 (mem.const_str $l6) (local.get $t2))
    (call $trace_i32 (mem.const_str $l7) (local.get $t3))

    (i32.add (i32.add (local.get $a) (local.get $b))
             (i32.add (local.get $c) (local.get $t3))))
)
