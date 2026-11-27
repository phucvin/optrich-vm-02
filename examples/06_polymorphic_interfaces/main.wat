;; Example 06 (Module 4/4) — Polymorphic Caller (`main`)
;; Calls `shape.scale(s, 2)` and `shape.area(s)` on a heterogeneous array of
;; `rect` and `square` trait objects — zero tables, zero magic slot numbers!
;;
;; Run:
;;   ./build/optrich-vm \
;;     --link shape=examples/06_polymorphic_interfaces/shape.wat \
;;     --link rect=examples/06_polymorphic_interfaces/rect.wat \
;;     --link square=examples/06_polymorphic_interfaces/square.wat \
;;     --gc-stats \
;;     examples/06_polymorphic_interfaces/main.wat main
(module
  (import "shape"  "area"       (func $area       (param ro_handle) (result i32)))
  (import "shape"  "scale"      (func $scale      (param ro_handle i32)))
  (import "rect"   "new_rect"   (func $new_rect   (param i32 i32) (result ro_handle)))
  (import "square" "new_square" (func $new_square (param i32) (result ro_handle)))

  (func (export "main") (result i32)
    (local $shapes rw_handle)
    (local $s ro_handle)
    (local $i i32)
    (local $n i32)
    (local $total i32)

    ;; Heterogeneous ref-slot array of 3 Shape trait objects:
    ;;   rect(3,4)  --scale(2)--> rect(6,8)   -> area 48
    ;;   square(5)  --scale(2)--> square(10)  -> area 100
    ;;   rect(6,7)  --scale(2)--> rect(12,14) -> area 168
    ;;   Total area after scaling = 48 + 100 + 168 = 316
    (local.set $shapes (mem.alloc (i32.const 0) (i32.const 3)))
    (mem.write_ref (local.get $shapes) (i32.const 0) (call $new_rect (i32.const 3) (i32.const 4)))
    (mem.write_ref (local.get $shapes) (i32.const 1) (call $new_square (i32.const 5)))
    (mem.write_ref (local.get $shapes) (i32.const 2) (call $new_rect (i32.const 6) (i32.const 7)))

    (local.set $n (mem.ref_len (local.get $shapes)))
    (block $done
      (loop $lp
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (mem.read_ref_ro (local.get $shapes) (local.get $i)))
        (call $scale (local.get $s) (i32.const 2))
        (local.set $total (i32.add (local.get $total) (call $area (local.get $s))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    (local.get $total))
)
