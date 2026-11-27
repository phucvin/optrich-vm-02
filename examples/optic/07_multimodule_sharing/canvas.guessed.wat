;; Guessed WAT for examples/optic/07_multimodule_sharing/canvas.opc
(module
  (global $DEFAULT_BG i32 (i32.const 46))
  (global $CREATED_COUNT (mut i32) (i32.const 0))

  ;; Canvas layout: bytes[0..4)=width, bytes[4..8)=height, refs[0]=pixels ([]u8)
  (func $Canvas__new (export "Canvas__new") (param $width i32) (param $height i32) (result rw_handle)
    (local $total i32)
    (local $pixels rw_handle)
    (local $i i32)
    (local $c rw_handle)
    (global.set $CREATED_COUNT (i32.add (global.get $CREATED_COUNT) (i32.const 1)))
    (local.set $total (i32.mul (local.get $width) (local.get $height)))
    (local.set $pixels (mem.alloc (local.get $total) (i32.const 0)))
    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $total)))
        (mem.write_u8 (local.get $pixels) (local.get $i) (global.get $DEFAULT_BG))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))
    (local.set $c (mem.alloc (i32.const 8) (i32.const 1)))
    (mem.write_i32 (local.get $c) (i32.const 0) (local.get $width))
    (mem.write_i32 (local.get $c) (i32.const 4) (local.get $height))
    (mem.write_ref (local.get $c) (i32.const 0) (local.get $pixels))
    (local.get $c))

  (func $Canvas__set_pixel (export "Canvas__set_pixel")
        (param $self rw_handle) (param $x i32) (param $y i32) (param $ch i32)
    (local $w i32)
    (local $h i32)
    (local.set $w (mem.read_i32 (local.get $self) (i32.const 0)))
    (local.set $h (mem.read_i32 (local.get $self) (i32.const 4)))
    (if (i32.and
          (i32.and (i32.ge_s (local.get $x) (i32.const 0)) (i32.lt_s (local.get $x) (local.get $w)))
          (i32.and (i32.ge_s (local.get $y) (i32.const 0)) (i32.lt_s (local.get $y) (local.get $h))))
      (then
        (mem.write_u8
          (mem.read_ref_rw (local.get $self) (i32.const 0))
          (i32.add (i32.mul (local.get $y) (local.get $w)) (local.get $x))
          (local.get $ch)))))

  (func $Canvas__fill_rect (export "Canvas__fill_rect")
        (param $self rw_handle) (param $x i32) (param $y i32) (param $w i32) (param $h i32) (param $ch i32)
    (local $dy i32)
    (local $dx i32)
    (block $break_y
      (loop $cont_y
        (br_if $break_y (i32.ge_s (local.get $dy) (local.get $h)))
        (local.set $dx (i32.const 0))
        (block $break_x
          (loop $cont_x
            (br_if $break_x (i32.ge_s (local.get $dx) (local.get $w)))
            (call $Canvas__set_pixel
              (local.get $self)
              (i32.add (local.get $x) (local.get $dx))
              (i32.add (local.get $y) (local.get $dy))
              (local.get $ch))
            (local.set $dx (i32.add (local.get $dx) (i32.const 1)))
            (br $cont_x)))
        (local.set $dy (i32.add (local.get $dy) (i32.const 1)))
        (br $cont_y))))

  (func $Canvas__width (export "Canvas__width") (param $self ro_handle) (result i32)
    (mem.read_i32 (local.get $self) (i32.const 0)))

  (func $Canvas__height (export "Canvas__height") (param $self ro_handle) (result i32)
    (mem.read_i32 (local.get $self) (i32.const 4)))

  (func $Canvas__get_pixel (export "Canvas__get_pixel")
        (param $self ro_handle) (param $x i32) (param $y i32) (result i32)
    (local $w i32)
    (local $h i32)
    (local.set $w (mem.read_i32 (local.get $self) (i32.const 0)))
    (local.set $h (mem.read_i32 (local.get $self) (i32.const 4)))
    (if (result i32)
      (i32.and
        (i32.and (i32.ge_s (local.get $x) (i32.const 0)) (i32.lt_s (local.get $x) (local.get $w)))
        (i32.and (i32.ge_s (local.get $y) (i32.const 0)) (i32.lt_s (local.get $y) (local.get $h))))
      (then
        (mem.read_u8
          (mem.read_ref_ro (local.get $self) (i32.const 0))
          (i32.add (i32.mul (local.get $y) (local.get $w)) (local.get $x))))
      (else
        (i32.const 0))))

  (func $Canvas__row (export "Canvas__row") (param $self ro_handle) (param $y i32) (result ro_handle)
    (local $w i32)
    (local.set $w (mem.read_i32 (local.get $self) (i32.const 0)))
    (mem.span
      (mem.read_ref_ro (local.get $self) (i32.const 0))
      (i32.mul (local.get $y) (local.get $w))
      (local.get $w)))

  (func (export "get_created_count") (result i32)
    (global.get $CREATED_COUNT))
)
