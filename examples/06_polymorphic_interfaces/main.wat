;; Example 06 (Module 5/5) — Polymorphic Multi-Interface Caller (`main`)
;; Demonstrates:
;;   1. Dynamic collision-free vtable registration via `ref.func` + `table.grow` + `(start $init)`
;;   2. Heterogeneous ref-array of `Shape` trait objects (`rect` + `square`)
;;   3. The SAME `rect` handle implementing TWO independent interfaces (`Shape` + `Perimeter`)
;;
;; Run:
;;   ./build/optrich-vm \
;;     --link shape_iface=examples/06_polymorphic_interfaces/shape_iface.wat \
;;     --link perimeter_iface=examples/06_polymorphic_interfaces/perimeter_iface.wat \
;;     --link rect=examples/06_polymorphic_interfaces/rect.wat \
;;     --link square=examples/06_polymorphic_interfaces/square.wat \
;;     --gc-stats \
;;     examples/06_polymorphic_interfaces/main.wat main
(module
  (import "shape_iface"     "area"         (func $area         (param ro_handle) (result i32)))
  (import "perimeter_iface" "perimeter"    (func $perimeter    (param ro_handle) (result i32)))
  (import "rect"            "new_rect"     (func $new_rect     (param i32 i32) (result handle)))
  (import "rect"            "as_shape"     (func $rect_shape   (param handle) (result ro_handle)))
  (import "rect"            "as_perimeter" (func $rect_perim   (param handle) (result ro_handle)))
  (import "square"          "new_square"   (func $new_square   (param i32) (result handle)))
  (import "square"          "as_shape"     (func $square_shape (param handle) (result ro_handle)))

  (func (export "main") (result i32)
    (local $r1 handle)
    (local $shapes rw_handle)
    (local $i i32)
    (local $n i32)
    (local $total i32)

    ;; Create r1 = rect(3, 4); use it as BOTH a Shape AND a Perimeter!
    (local.set $r1 (call $new_rect (i32.const 3) (i32.const 4)))

    ;; Heterogeneous array of 3 Shape trait objects:
    ;;   rect(3,4) -> area 12, square(5) -> area 25, rect(6,7) -> area 42 => sum = 79
    (local.set $shapes (mem.alloc (i32.const 0) (i32.const 3)))
    (mem.write_ref (local.get $shapes) (i32.const 0) (call $rect_shape (local.get $r1)))
    (mem.write_ref (local.get $shapes) (i32.const 1) (call $square_shape (call $new_square (i32.const 5))))
    (mem.write_ref (local.get $shapes) (i32.const 2) (call $rect_shape (call $new_rect (i32.const 6) (i32.const 7))))

    (local.set $n (mem.ref_len (local.get $shapes)))
    (block $done
      (loop $lp
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $total
          (i32.add (local.get $total)
                   (call $area (mem.readonly (mem.read_ref (local.get $shapes) (local.get $i))))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))

    ;; Also invoke the second interface (Perimeter) on the SAME rect r1: 2*(3+4) = 14
    ;; Total = 79 + 14 = 93
    (i32.add (local.get $total) (call $perimeter (call $rect_perim (local.get $r1)))))
)
