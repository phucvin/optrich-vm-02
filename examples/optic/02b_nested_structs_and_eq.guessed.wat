;; Guessed WAT for examples/optic/02b_nested_structs_and_eq.opc
;; Point: bytes[0..4)=x, bytes[4..8)=y => mem.alloc(8, 0)
;; Rect : bytes[0..4)=w, bytes[4..8)=h, refs[0]=origin => mem.alloc(8, 1)
(module
  (import "host" "trace_i32"  (func $trace_i32  (param ro_handle i32)))
  (import "host" "trace_meta" (func $trace_meta (param ro_handle ro_handle)))

  (string $l1 "step1.same_id")
  (string $l2 "step2.diff_id")
  (string $l3 "step3.val_eq")
  (string $l4 "step4.rect_meta")
  (string $l5 "step5.rect_origin_x")
  (string $l6 "step6.rect_area")
  (string $l7 "step7.tail_meta")
  (string $l8 "step8.tail_sum_x")

  (func $Point__new (param $x i32) (param $y i32) (result rw_handle)
    (local $p rw_handle)
    (local.set $p (mem.alloc (i32.const 8) (i32.const 0)))
    (mem.write_i32 (local.get $p) (i32.const 0) (local.get $x))
    (mem.write_i32 (local.get $p) (i32.const 4) (local.get $y))
    (local.get $p))

  (func $Point__equals (param $self ro_handle) (param $other ro_handle) (result i32)
    (if (result i32)
      (i32.eq (mem.read_i32 (local.get $self) (i32.const 0))
              (mem.read_i32 (local.get $other) (i32.const 0)))
      (then
        (i32.eq (mem.read_i32 (local.get $self) (i32.const 4))
                (mem.read_i32 (local.get $other) (i32.const 4))))
      (else (i32.const 0))))

  (func $Rect__area (param $self ro_handle) (result i32)
    (i32.mul
      (mem.read_i32 (local.get $self) (i32.const 0))
      (mem.read_i32 (local.get $self) (i32.const 4))))

  (func $sum_x (param $pts ro_handle) (result i32)
    (local $acc i32)
    (local $i i32)
    (local $end i32)
    (local.set $acc (i32.const 0))
    (local.set $i (i32.const 0))
    (local.set $end (mem.ref_len (local.get $pts)))
    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $end)))
        (local.set $acc
          (i32.add (local.get $acc)
                   (mem.read_i32 (mem.read_ref_ro (local.get $pts) (local.get $i)) (i32.const 0))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))
    (local.get $acc))

  (func (export "main") (result i32)
    (local $p1 rw_handle)
    (local $p2 rw_handle)
    (local $p1_alias rw_handle)
    (local $r rw_handle)
    (local $pts rw_handle)
    (local $tail ro_handle)
    (local $sx i32)

    (local.set $p1 (call $Point__new (i32.const 10) (i32.const 20)))
    (local.set $p2 (call $Point__new (i32.const 10) (i32.const 20)))
    (local.set $p1_alias (local.get $p1))

    (call $trace_i32 (mem.const_str $l1) (mem.eq (local.get $p1) (local.get $p1_alias)))
    (call $trace_i32 (mem.const_str $l2) (mem.eq (local.get $p1) (local.get $p2)))
    (call $trace_i32 (mem.const_str $l3)
      (call $Point__equals (local.get $p1) (local.get $p2)))

    (local.set $r (mem.alloc (i32.const 8) (i32.const 1)))
    (mem.write_i32 (local.get $r) (i32.const 0) (i32.const 5))
    (mem.write_i32 (local.get $r) (i32.const 4) (i32.const 4))
    (mem.write_ref (local.get $r) (i32.const 0) (local.get $p1))

    (call $trace_meta (mem.const_str $l4) (local.get $r))
    (call $trace_i32 (mem.const_str $l5)
      (mem.read_i32 (mem.read_ref_ro (local.get $r) (i32.const 0)) (i32.const 0)))
    (call $trace_i32 (mem.const_str $l6) (call $Rect__area (local.get $r)))

    (local.set $pts (mem.alloc (i32.const 0) (i32.const 3)))
    (mem.write_ref (local.get $pts) (i32.const 0) (call $Point__new (i32.const 1) (i32.const 0)))
    (mem.write_ref (local.get $pts) (i32.const 1) (call $Point__new (i32.const 10) (i32.const 0)))
    (mem.write_ref (local.get $pts) (i32.const 2) (call $Point__new (i32.const 100) (i32.const 0)))

    (local.set $tail (mem.readonly (mem.ref_span (local.get $pts) (i32.const 1) (i32.const 2))))
    (call $trace_meta (mem.const_str $l7) (local.get $tail))

    (local.set $sx (call $sum_x (local.get $tail)))
    (call $trace_i32 (mem.const_str $l8) (local.get $sx))

    (i32.add (local.get $sx) (call $Rect__area (local.get $r))))
)
