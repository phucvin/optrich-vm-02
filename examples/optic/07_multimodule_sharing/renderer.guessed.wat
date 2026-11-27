;; Guessed WAT for examples/optic/07_multimodule_sharing/renderer.opc
(module
  (import "host"   "io_write"       (func $io_write       (param ro_handle ro_handle) (result i32)))
  (import "canvas" "Canvas__height" (func $Canvas__height (param ro_handle) (result i32)))
  (import "canvas" "Canvas__row"    (func $Canvas__row    (param ro_handle i32) (result ro_handle)))

  (global $INSPECT_COUNT (mut i32) (i32.const 0))

  (func (export "count_ink") (param $c ro_handle) (param $ch i32) (result i32)
    (local $total i32)
    (local $y i32)
    (local $h i32)
    (local $line ro_handle)
    (local $x i32)
    (local $len i32)
    (global.set $INSPECT_COUNT (i32.add (global.get $INSPECT_COUNT) (i32.const 1)))
    (local.set $h (call $Canvas__height (local.get $c)))
    (block $break_y
      (loop $cont_y
        (br_if $break_y (i32.ge_s (local.get $y) (local.get $h)))
        (local.set $line (call $Canvas__row (local.get $c) (local.get $y)))
        (local.set $len (mem.byte_len (local.get $line)))
        (local.set $x (i32.const 0))
        (block $break_x
          (loop $cont_x
            (br_if $break_x (i32.ge_s (local.get $x) (local.get $len)))
            (if (i32.eq (mem.read_u8 (local.get $line) (local.get $x)) (local.get $ch))
              (then
                (local.set $total (i32.add (local.get $total) (i32.const 1)))))
            (local.set $x (i32.add (local.get $x) (i32.const 1)))
            (br $cont_x)))
        (local.set $y (i32.add (local.get $y) (i32.const 1)))
        (br $cont_y)))
    (local.get $total))

  (func (export "render_row") (param $label ro_handle) (param $c ro_handle) (param $y i32) (result i32)
    (call $io_write (local.get $label) (call $Canvas__row (local.get $c) (local.get $y))))

  (func (export "get_inspect_count") (result i32)
    (global.get $INSPECT_COUNT))
)
