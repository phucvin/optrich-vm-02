;; Guessed WAT for examples/optic/08b_cross_module_dyn_trait/main.opc
(module
  (import "host"    "trace_i32"   (func $trace_i32   (param ro_handle i32)))
  (import "widgets" "make_button" (func $make_button (param i32 i32) (result ro_handle)))
  (import "widgets" "make_slider" (func $make_slider (param i32 i32) (result ro_handle)))

  (type $sig_width (func (param ro_handle) (result i32)))

  (string $l1 "step1.button1_width")
  (string $l2 "step2.slider_width")
  (string $l3 "step3.button2_width")

  (func $dyn_Renderable__width (param $obj ro_handle) (result i32)
    (call_ref (type $sig_width)
      (mem.read_ref_ro (local.get $obj) (i32.const 0))
      (mem.read_func (mem.read_ref_ro (local.get $obj) (i32.const 1)) (i32.const 0))))

  (func (export "main") (result i32)
    (local $w1 ro_handle)
    (local $w2 ro_handle)
    (local $w3 ro_handle)
    (local $a i32)
    (local $b i32)
    (local $c i32)

    (local.set $w1 (call $make_button (i32.const 12) (i32.const 4)))
    (local.set $w2 (call $make_slider (i32.const 10) (i32.const 65)))
    (local.set $w3 (call $make_button (i32.const 6) (i32.const 2)))

    (local.set $a (call $dyn_Renderable__width (local.get $w1)))
    (local.set $b (call $dyn_Renderable__width (local.get $w2)))
    (local.set $c (call $dyn_Renderable__width (local.get $w3)))

    (call $trace_i32 (mem.const_str $l1) (local.get $a))
    (call $trace_i32 (mem.const_str $l2) (local.get $b))
    (call $trace_i32 (mem.const_str $l3) (local.get $c))

    (i32.add (i32.add (local.get $a) (local.get $b)) (local.get $c)))
)
