;; Guessed WAT for examples/optic/08_iterators_and_dyn_trait.opc
;; RangeStep: mem.alloc(12, 0) -> bytes[0..4)=cur, bytes[4..8)=end, bytes[8..12)=step
;; dyn Shape: frozen mem.alloc(0, 2) -> refs[0]=data, refs[1]=frozen vtable (slot 0=area, slot 1=scale)
(module
  (import "host" "trace_i32"  (func $trace_i32  (param ro_handle i32)))
  (import "host" "trace_meta" (func $trace_meta (param ro_handle handle)))

  (type $sig_area  (func (param handle) (result i32)))
  (type $sig_scale (func (param handle i32)))

  (global $Rect_Shape_vt   (mut handle) (handle.null))
  (global $Square_Shape_vt (mut handle) (handle.null))

  (string $l1 "step1.iter_count")
  (string $l2 "step2.iter_sum")
  (string $l3 "step3.dyn_obj_meta")
  (string $l4 "step4.area_before")
  (string $l5 "step5.area_after")

  ;; --- RangeStep iterator ---
  (func $RangeStep__new (param $start i32) (param $end i32) (param $step i32) (result rw_handle)
    (local $it rw_handle)
    (local.set $it (mem.alloc (i32.const 12) (i32.const 0)))
    (mem.write_i32 (local.get $it) (i32.const 0) (local.get $start))
    (mem.write_i32 (local.get $it) (i32.const 4) (local.get $end))
    (mem.write_i32 (local.get $it) (i32.const 8) (local.get $step))
    (local.get $it))

  (func $RangeStep__next (param $self rw_handle) (result i32 i32)
    (local $cur i32)
    (local.set $cur (mem.read_i32 (local.get $self) (i32.const 0)))
    (if (i32.ge_s (local.get $cur) (mem.read_i32 (local.get $self) (i32.const 4)))
      (then (return (i32.const 0) (i32.const 0))))
    (mem.write_i32 (local.get $self) (i32.const 0)
      (i32.add (local.get $cur) (mem.read_i32 (local.get $self) (i32.const 8))))
    (i32.const 1) (local.get $cur))

  ;; --- Rect impl Shape ---
  (func $Rect__area (param $self handle) (result i32)
    (i32.mul (mem.read_i32 (local.get $self) (i32.const 0))
             (mem.read_i32 (local.get $self) (i32.const 4))))

  (func $Rect__scale (param $self handle) (param $factor i32)
    (mem.write_i32 (local.get $self) (i32.const 0)
      (i32.mul (mem.read_i32 (local.get $self) (i32.const 0)) (local.get $factor)))
    (mem.write_i32 (local.get $self) (i32.const 4)
      (i32.mul (mem.read_i32 (local.get $self) (i32.const 4)) (local.get $factor))))

  (func $Rect__as_dyn_Shape (param $w i32) (param $h i32) (result ro_handle)
    (local $vt rw_handle)
    (local $data rw_handle)
    (local $obj rw_handle)
    (if (mem.is_null (global.get $Rect_Shape_vt))
      (then
        (local.set $vt (mem.alloc_vtable (i32.const 2)))
        (mem.write_func (local.get $vt) (i32.const 0) (ref.func $Rect__area))
        (mem.write_func (local.get $vt) (i32.const 1) (ref.func $Rect__scale))
        (global.set $Rect_Shape_vt (mem.readonly (local.get $vt)))))
    (local.set $data (mem.alloc (i32.const 8) (i32.const 0)))
    (mem.write_i32 (local.get $data) (i32.const 0) (local.get $w))
    (mem.write_i32 (local.get $data) (i32.const 4) (local.get $h))
    (local.set $obj (mem.alloc (i32.const 0) (i32.const 2)))
    (mem.write_ref (local.get $obj) (i32.const 0) (local.get $data))
    (mem.write_ref (local.get $obj) (i32.const 1) (global.get $Rect_Shape_vt))
    (mem.readonly (local.get $obj)))

  ;; --- Square impl Shape ---
  (func $Square__area (param $self handle) (result i32)
    (local $s i32)
    (local.set $s (mem.read_i32 (local.get $self) (i32.const 0)))
    (i32.mul (local.get $s) (local.get $s)))

  (func $Square__scale (param $self handle) (param $factor i32)
    (mem.write_i32 (local.get $self) (i32.const 0)
      (i32.mul (mem.read_i32 (local.get $self) (i32.const 0)) (local.get $factor))))

  (func $Square__as_dyn_Shape (param $side i32) (result ro_handle)
    (local $vt rw_handle)
    (local $data rw_handle)
    (local $obj rw_handle)
    (if (mem.is_null (global.get $Square_Shape_vt))
      (then
        (local.set $vt (mem.alloc_vtable (i32.const 2)))
        (mem.write_func (local.get $vt) (i32.const 0) (ref.func $Square__area))
        (mem.write_func (local.get $vt) (i32.const 1) (ref.func $Square__scale))
        (global.set $Square_Shape_vt (mem.readonly (local.get $vt)))))
    (local.set $data (mem.alloc (i32.const 4) (i32.const 0)))
    (mem.write_i32 (local.get $data) (i32.const 0) (local.get $side))
    (local.set $obj (mem.alloc (i32.const 0) (i32.const 2)))
    (mem.write_ref (local.get $obj) (i32.const 0) (local.get $data))
    (mem.write_ref (local.get $obj) (i32.const 1) (global.get $Square_Shape_vt))
    (mem.readonly (local.get $obj)))

  ;; --- dyn Shape dispatch helpers ---
  (func $dyn_Shape__area (param $obj handle) (result i32)
    (call_ref (type $sig_area)
      (mem.read_ref (local.get $obj) (i32.const 0))
      (mem.read_func (mem.read_ref (local.get $obj) (i32.const 1)) (i32.const 0))))

  (func $dyn_Shape__scale (param $obj handle) (param $factor i32)
    (call_ref (type $sig_scale)
      (mem.read_ref (local.get $obj) (i32.const 0))
      (local.get $factor)
      (mem.read_func (mem.read_ref (local.get $obj) (i32.const 1)) (i32.const 1))))

  (func (export "main") (result i32)
    (local $it rw_handle)
    (local $ok i32)
    (local $x i32)
    (local $iter_sum i32)
    (local $iter_count i32)
    (local $shapes rw_handle)
    (local $i i32)
    (local $n i32)
    (local $cur handle)
    (local $before_total i32)
    (local $after_total i32)

    ;; 1. for x in RangeStep::new(2, 12, 3)
    (local.set $it (call $RangeStep__new (i32.const 2) (i32.const 12) (i32.const 3)))
    (block $done_it
      (loop $lp_it
        (call $RangeStep__next (local.get $it))
        (local.set $x)
        (local.set $ok)
        (br_if $done_it (i32.eqz (local.get $ok)))
        (local.set $iter_sum (i32.add (local.get $iter_sum) (local.get $x)))
        (local.set $iter_count (i32.add (local.get $iter_count) (i32.const 1)))
        (br $lp_it)))
    (call $trace_i32 (mem.const_str $l1) (local.get $iter_count))
    (call $trace_i32 (mem.const_str $l2) (local.get $iter_sum))

    ;; 2. Heterogeneous []dyn Shape
    (local.set $shapes (mem.alloc (i32.const 0) (i32.const 2)))
    (mem.write_ref (local.get $shapes) (i32.const 0) (call $Rect__as_dyn_Shape (i32.const 3) (i32.const 4)))
    (mem.write_ref (local.get $shapes) (i32.const 1) (call $Square__as_dyn_Shape (i32.const 5)))
    (call $trace_meta (mem.const_str $l3) (mem.read_ref (local.get $shapes) (i32.const 0)))

    (local.set $n (mem.ref_len (local.get $shapes)))
    (local.set $i (i32.const 0))
    (block $done_s
      (loop $lp_s
        (br_if $done_s (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $cur (mem.read_ref (local.get $shapes) (local.get $i)))
        (local.set $before_total (i32.add (local.get $before_total) (call $dyn_Shape__area (local.get $cur))))
        (call $dyn_Shape__scale (local.get $cur) (i32.const 2))
        (local.set $after_total (i32.add (local.get $after_total) (call $dyn_Shape__area (local.get $cur))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp_s)))
    (call $trace_i32 (mem.const_str $l4) (local.get $before_total))
    (call $trace_i32 (mem.const_str $l5) (local.get $after_total))

    (i32.add (local.get $iter_sum) (local.get $after_total)))
)
