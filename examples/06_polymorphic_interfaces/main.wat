;; Example 06 (Module 4/4) — Polymorphic Caller (`main`)
;; Stores heterogeneous `rect` and `square` handles in a split-block ref array
;; (`mem.alloc(0, 3)`) and sums their areas polymorphically via `shape_iface.area`.
;;
;; Run:
;;   ./build/optrich-vm \
;;     --link shape_iface=examples/06_polymorphic_interfaces/shape_iface.wat \
;;     --link rect=examples/06_polymorphic_interfaces/rect.wat \
;;     --link square=examples/06_polymorphic_interfaces/square.wat \
;;     --gc-stats \
;;     examples/06_polymorphic_interfaces/main.wat main
(module
  (import "shape_iface" "area"   (func $area       (param handle) (result i32)))
  (import "rect"        "new_rect"   (func $new_rect   (param i32 i32) (result handle)))
  (import "square"      "new_square" (func $new_square (param i32) (result handle)))

  (func (export "main") (result i32)
    (local $shapes rw_handle)
    (local $i i32)
    (local $n i32)
    (local $total i32)

    ;; Allocate a pure ref-slot array of 3 polymorphic shape handles
    (local.set $shapes (mem.alloc (i32.const 0) (i32.const 3)))
    (mem.write_ref (local.get $shapes) (i32.const 0) (call $new_rect (i32.const 3) (i32.const 4))) ;; 12
    (mem.write_ref (local.get $shapes) (i32.const 1) (call $new_square (i32.const 5)))            ;; 25
    (mem.write_ref (local.get $shapes) (i32.const 2) (call $new_rect (i32.const 6) (i32.const 7))) ;; 42

    (local.set $n (mem.ref_len (local.get $shapes)))
    (block $done
      (loop $lp
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $total
          (i32.add (local.get $total)
                   (call $area (mem.read_ref (local.get $shapes) (local.get $i)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    ;; 12 + 25 + 42 = 79
    (local.get $total))
)
