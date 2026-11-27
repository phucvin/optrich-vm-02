;; Guessed WAT for examples/optic/04_enums_and_match.opc
;; BoxedTag: mem.alloc(4, 0)  -> bytes[0..4)=code
;; Shape   : mem.alloc(24, 2) -> bytes[0..4)=tag, bytes[8..16)=r|w, bytes[16..24)=h, refs[0]=name, refs[1]=tag
(module
  (import "host" "trace_i32"  (func $trace_i32  (param ro_handle i32)))
  (import "host" "trace_f64"  (func $trace_f64  (param ro_handle f64)))
  (import "host" "trace_meta" (func $trace_meta (param ro_handle ro_handle)))
  (import "host" "trace_gc"   (func $trace_gc   (param ro_handle)))

  (string $l1 "step1.circle_area")
  (string $l2 "step2.rect_area")
  (string $l3 "step3.labelled_code")
  (string $l4 "step4.s3_meta")
  (string $l5 "step5.gc_after_variant_swap")
  (string $l6 "step6.opt_prim_none")
  (string $l7 "step7.opt_prim_some")
  (string $l8 "step8.opt_ref_some")
  (string $s_hero "hero")

  ;; Constructors
  (func $BoxedTag__new (param $code i32) (result rw_handle)
    (local $b rw_handle)
    (local.set $b (mem.alloc (i32.const 4) (i32.const 0)))
    (mem.write_i32 (local.get $b) (i32.const 0) (local.get $code))
    (local.get $b))

  (func $Shape__Circle (param $r f64) (result rw_handle)
    (local $s rw_handle)
    (local.set $s (mem.alloc (i32.const 24) (i32.const 2)))
    (mem.write_i32 (local.get $s) (i32.const 0) (i32.const 0))
    (mem.write_f64 (local.get $s) (i32.const 8) (local.get $r))
    (local.get $s))

  (func $Shape__Rect (param $w f64) (param $h f64) (result rw_handle)
    (local $s rw_handle)
    (local.set $s (mem.alloc (i32.const 24) (i32.const 2)))
    (mem.write_i32 (local.get $s) (i32.const 0) (i32.const 1))
    (mem.write_f64 (local.get $s) (i32.const 8) (local.get $w))
    (mem.write_f64 (local.get $s) (i32.const 16) (local.get $h))
    (local.get $s))

  (func $Shape__Labelled (param $name ro_handle) (param $tag rw_handle) (result rw_handle)
    (local $s rw_handle)
    (local.set $s (mem.alloc (i32.const 24) (i32.const 2)))
    (mem.write_i32 (local.get $s) (i32.const 0) (i32.const 2))
    (mem.write_ref (local.get $s) (i32.const 0) (local.get $name))
    (mem.write_ref (local.get $s) (i32.const 1) (local.get $tag))
    (local.get $s))

  ;; match lowered via br_table
  (func $shape_area (param $s ro_handle) (result f64)
    (local $r f64)
    (block $default
      (block $arm_labelled
        (block $arm_rect
          (block $arm_circle
            (br_table $arm_circle $arm_rect $arm_labelled $default
              (mem.read_i32 (local.get $s) (i32.const 0))))
          ;; arm Circle(r)
          (local.set $r (mem.read_f64 (local.get $s) (i32.const 8)))
          (return (f64.mul (f64.const 3.14) (f64.mul (local.get $r) (local.get $r)))))
        ;; arm Rect(w, h)
        (return (f64.mul (mem.read_f64 (local.get $s) (i32.const 8))
                         (mem.read_f64 (local.get $s) (i32.const 16)))))
      ;; arm Labelled(_, tag)
      (return (f64.convert_i32_s
                (mem.read_i32 (mem.read_ref_ro (local.get $s) (i32.const 1)) (i32.const 0)))))
    (unreachable))

  ;; In-place variant overwrite with GC-safe nulling of inactive ref slots
  (func $shape_reset_to_circle (param $s rw_handle) (param $r f64)
    (mem.write_i32 (local.get $s) (i32.const 0) (i32.const 0))
    (mem.write_f64 (local.get $s) (i32.const 8) (local.get $r))
    (mem.write_ref (local.get $s) (i32.const 0) (handle.null))
    (mem.write_ref (local.get $s) (i32.const 1) (handle.null)))

  ;; Unboxed Option<i32> -> (is_some: i32, val: i32)
  (func $find_even (param $a i32) (param $b i32) (result i32 i32)
    (if (i32.eqz (i32.rem_s (local.get $a) (i32.const 2)))
      (then (return (i32.const 1) (local.get $a))))
    (if (i32.eqz (i32.rem_s (local.get $b) (i32.const 2)))
      (then (return (i32.const 1) (local.get $b))))
    (i32.const 0) (i32.const 0))

  ;; Niche-optimized Option<BoxedTag> -> nullable rw_handle
  (func $maybe_tag (param $create i32) (result rw_handle)
    (if (result rw_handle) (local.get $create)
      (then (call $BoxedTag__new (i32.const 77)))
      (else (handle.null))))

  (func (export "main") (result i32)
    (local $s1 rw_handle)
    (local $s2 rw_handle)
    (local $s3 rw_handle)
    (local $code i32)
    (local $opt_ok i32)
    (local $opt_val i32)
    (local $n1 i32)
    (local $n2 i32)
    (local $t rw_handle)
    (local $t_code i32)

    (local.set $s1 (call $Shape__Circle (f64.const 2.0)))
    (local.set $s2 (call $Shape__Rect (f64.const 3.0) (f64.const 5.0)))
    (local.set $s3 (call $Shape__Labelled (mem.const_str $s_hero) (call $BoxedTag__new (i32.const 42))))

    (call $trace_f64 (mem.const_str $l1) (call $shape_area (local.get $s1)))
    (call $trace_f64 (mem.const_str $l2) (call $shape_area (local.get $s2)))
    (local.set $code (i32.trunc_f64_s (call $shape_area (local.get $s3))))
    (call $trace_i32 (mem.const_str $l3) (local.get $code))
    (call $trace_meta (mem.const_str $l4) (local.get $s3))

    ;; Overwrite s3 in-place; old BoxedTag(42) is nulled out and swept!
    (call $shape_reset_to_circle (local.get $s3) (f64.const 1.0))
    ;; Live: 9 const strings + s1 + s2 + s3 = 12
    ;; Swept: 1 BoxedTag(42) (zero temp wrappers thanks to static rw_handle <: ro_handle!)
    (call $trace_gc (mem.const_str $l5))

    ;; Unboxed Option<i32> (None)
    (call $find_even (i32.const 3) (i32.const 5))
    (local.set $opt_val)
    (local.set $opt_ok)
    (local.set $n1
      (if (result i32) (local.get $opt_ok)
        (then (local.get $opt_val))
        (else (i32.const -1))))
    (call $trace_i32 (mem.const_str $l6) (local.get $n1))

    ;; Unboxed Option<i32> (Some(6))
    (call $find_even (i32.const 3) (i32.const 6))
    (local.set $opt_val)
    (local.set $opt_ok)
    (local.set $n2
      (if (result i32) (local.get $opt_ok)
        (then (local.get $opt_val))
        (else (i32.const -1))))
    (call $trace_i32 (mem.const_str $l7) (local.get $n2))

    ;; Niche-optimized Option<BoxedTag>
    (local.set $t (call $maybe_tag (i32.const 1)))
    (local.set $t_code
      (if (result i32) (i32.eqz (mem.is_null (local.get $t)))
        (then (mem.read_i32 (local.get $t) (i32.const 0)))
        (else (i32.const 0))))
    (call $trace_i32 (mem.const_str $l8) (local.get $t_code))

    (i32.add (i32.add (local.get $code) (local.get $n2)) (local.get $t_code)))
)
