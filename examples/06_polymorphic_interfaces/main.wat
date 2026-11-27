;; Example 06 (Module 3/3) — Polymorphic Caller (`main`)
;; Dispatches `area` (vtable[0]) and `perimeter` (vtable[1]) directly via `call_ref`
;; on a heterogeneous ref-array of `rect` and `square` trait objects.
;; Zero tables, zero slot coordination, zero interface-registry boilerplate!
;;
;; Run:
;;   ./build/optrich-vm \
;;     --link rect=examples/06_polymorphic_interfaces/rect.wat \
;;     --link square=examples/06_polymorphic_interfaces/square.wat \
;;     --gc-stats \
;;     examples/06_polymorphic_interfaces/main.wat main
(module
  (type $shape_method (func (param handle) (result i32)))

  (import "rect"   "new_rect"   (func $new_rect   (param i32 i32) (result ro_handle)))
  (import "square" "new_square" (func $new_square (param i32) (result ro_handle)))

  ;; Call method `slot` (0 = area, 1 = perimeter) on any Shape trait object via `call_ref`
  (func $call_shape (param $trait handle) (param $method_slot i32) (result i32)
    (call_ref (type $shape_method)
      (mem.read_ref (local.get $trait) (i32.const 0))                                    ;; self data
      (mem.read_func (mem.read_ref (local.get $trait) (i32.const 1)) (local.get $method_slot))))

  (func (export "main") (result i32)
    (local $shapes rw_handle)
    (local $s handle)
    (local $i i32)
    (local $n i32)
    (local $total i32)

    ;; Heterogeneous ref-slot array of 3 Shape trait objects:
    ;;   rect(3,4)  -> area 12, perim 14
    ;;   square(5)  -> area 25, perim 20
    ;;   rect(6,7)  -> area 42, perim 26
    ;;   Grand total = (12+25+42) + (14+20+26) = 79 + 60 = 139
    (local.set $shapes (mem.alloc (i32.const 0) (i32.const 3)))
    (mem.write_ref (local.get $shapes) (i32.const 0) (call $new_rect (i32.const 3) (i32.const 4)))
    (mem.write_ref (local.get $shapes) (i32.const 1) (call $new_square (i32.const 5)))
    (mem.write_ref (local.get $shapes) (i32.const 2) (call $new_rect (i32.const 6) (i32.const 7)))

    (local.set $n (mem.ref_len (local.get $shapes)))
    (block $done
      (loop $lp
        (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
        (local.set $s (mem.read_ref (local.get $shapes) (local.get $i)))
        (local.set $total
          (i32.add (local.get $total)
            (i32.add
              (call $call_shape (local.get $s) (i32.const 0))   ;; area
              (call $call_shape (local.get $s) (i32.const 1))))) ;; perimeter
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    (local.get $total))
)
