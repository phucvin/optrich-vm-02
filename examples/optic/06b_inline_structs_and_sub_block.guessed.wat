;; Guessed WAT for examples/optic/06b_inline_structs_and_sub_block.opc
;; TagPoint: bytes[0..4)=x, bytes[4..8)=y, refs[0]=label => (8 bytes, 1 ref)
;; Sprite  : bytes[0..4)=id, bytes[4..12)=inline pos, refs[0]=pos.label => mem.alloc(12, 1)
;; []inline TagPoint (len=3): mem.alloc(24, 3) (stride: 8 bytes, 1 ref)
(module
  (import "host" "trace_i32"  (func $trace_i32  (param ro_handle i32)))
  (import "host" "trace_str"  (func $trace_str  (param ro_handle ro_handle)))
  (import "host" "trace_meta" (func $trace_meta (param ro_handle handle)))

  (string $l1  "step1.sprite_meta")
  (string $l2  "step2.direct_x")
  (string $l3  "step3.aliased_x")
  (string $l4  "step4.aliased_y")
  (string $l5  "step5.ro_sub_meta")
  (string $l6  "step6.ro_sub_label")
  (string $l7  "step7.ro_sub_sum")
  (string $l8  "step8.flat_arr_meta")
  (string $l9  "step9.detached_x")
  (string $l10 "step10.orig_elem1_x")

  (string $s_hero "hero")
  (string $s_p0   "p0")
  (string $s_p1   "p1")
  (string $s_p2   "p2")

  (func $shift_point (param $p rw_handle) (param $dx i32) (param $dy i32)
    (mem.write_i32 (local.get $p) (i32.const 0)
      (i32.add (mem.read_i32 (local.get $p) (i32.const 0)) (local.get $dx)))
    (mem.write_i32 (local.get $p) (i32.const 4)
      (i32.add (mem.read_i32 (local.get $p) (i32.const 4)) (local.get $dy))))

  (func $read_point_sum (param $p ro_handle) (result i32)
    (i32.add (mem.read_i32 (local.get $p) (i32.const 0))
             (mem.read_i32 (local.get $p) (i32.const 4))))

  (func (export "main") (result i32)
    (local $s rw_handle)
    (local $ro_s ro_handle)
    (local $ro_p ro_handle)
    (local $psum i32)
    (local $pts rw_handle)
    (local $detached rw_handle)

    ;; 1. Single allocation for Sprite with inline TagPoint
    (local.set $s (mem.alloc (i32.const 12) (i32.const 1)))
    (mem.write_i32 (local.get $s) (i32.const 0) (i32.const 1))
    (mem.write_i32 (local.get $s) (i32.const 4) (i32.const 10))
    (mem.write_i32 (local.get $s) (i32.const 8) (i32.const 20))
    (mem.write_ref (local.get $s) (i32.const 0) (mem.const_str $s_hero))
    (call $trace_meta (mem.const_str $l1) (local.get $s))

    ;; Direct field read (zero sub_block overhead)
    (call $trace_i32 (mem.const_str $l2) (mem.read_i32 (local.get $s) (i32.const 4)))

    ;; Pass inline field as handle via mem.sub_block
    (call $shift_point
      (mem.sub_block (local.get $s) (i32.const 4) (i32.const 8) (i32.const 0) (i32.const 1))
      (i32.const 5) (i32.const -3))
    (call $trace_i32 (mem.const_str $l3) (mem.read_i32 (local.get $s) (i32.const 4)))
    (call $trace_i32 (mem.const_str $l4) (mem.read_i32 (local.get $s) (i32.const 8)))

    ;; Read-only sub_block preserves ro capability
    (local.set $ro_s (mem.readonly (local.get $s)))
    (local.set $ro_p (mem.sub_block (local.get $ro_s) (i32.const 4) (i32.const 8) (i32.const 0) (i32.const 1)))
    (call $trace_meta (mem.const_str $l5) (local.get $ro_p))
    (call $trace_str (mem.const_str $l6) (mem.readonly (mem.read_ref (local.get $ro_p) (i32.const 0))))
    (local.set $psum (call $read_point_sum (local.get $ro_p)))
    (call $trace_i32 (mem.const_str $l7) (local.get $psum))

    ;; 2. Flat array of 3 inline TagPoints -> single mem.alloc(24, 3)
    (local.set $pts (mem.alloc (i32.const 24) (i32.const 3)))
    (mem.write_i32 (local.get $pts) (i32.const 0) (i32.const 1))
    (mem.write_i32 (local.get $pts) (i32.const 4) (i32.const 2))
    (mem.write_ref (local.get $pts) (i32.const 0) (mem.const_str $s_p0))

    (mem.write_i32 (local.get $pts) (i32.const 8) (i32.const 10))
    (mem.write_i32 (local.get $pts) (i32.const 12) (i32.const 20))
    (mem.write_ref (local.get $pts) (i32.const 1) (mem.const_str $s_p1))

    (mem.write_i32 (local.get $pts) (i32.const 16) (i32.const 100))
    (mem.write_i32 (local.get $pts) (i32.const 20) (i32.const 200))
    (mem.write_ref (local.get $pts) (i32.const 2) (mem.const_str $s_p2))
    (call $trace_meta (mem.const_str $l8) (local.get $pts))

    ;; Detach element 1 via clone_span(sub_block(...))
    (local.set $detached
      (mem.clone_span (mem.sub_block (local.get $pts) (i32.const 8) (i32.const 8) (i32.const 1) (i32.const 1))))
    (call $shift_point (local.get $detached) (i32.const 7) (i32.const 8))
    (call $trace_i32 (mem.const_str $l9) (mem.read_i32 (local.get $detached) (i32.const 0)))
    (call $trace_i32 (mem.const_str $l10) (mem.read_i32 (local.get $pts) (i32.const 8)))

    (i32.add (local.get $psum)
             (i32.add (mem.read_i32 (local.get $detached) (i32.const 0))
                      (mem.read_i32 (local.get $pts) (i32.const 8)))))
)
