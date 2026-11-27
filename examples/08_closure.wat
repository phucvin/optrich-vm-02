;; Example 08 — Simple Closures (`make_adder`) via Split Blocks + `call_ref`
;; A closure is a tiny split block:
;;   bytes[0..4) : captured `i32` environment variable (`k`)
;;   refs[0]     : frozen 1-method vtable holding the closure's code `funcref`
;;
;; Run: ./build/optrich-vm examples/08_closure.wat main
(module
  (type $fn_i32_to_i32 (func (param ro_handle i32) (result i32)))

  ;; Shared vtable for all `adder` closures
  (global $adder_vt (mut ro_handle) (handle.null))

  ;; Closure body: returns captured `k` + `x`
  (func $adder_body (param $env ro_handle) (param $x i32) (result i32)
    (i32.add (mem.read_i32 (local.get $env) (i32.const 0)) (local.get $x)))

  ;; Constructor: make_adder(k) -> closure handle
  (func $make_adder (param $k i32) (result ro_handle)
    (local $vt rw_handle)
    (local $cl rw_handle)

    (if (mem.is_null (global.get $adder_vt))
      (then
        (local.set $vt (mem.alloc_vtable (i32.const 1)))
        (mem.write_func (local.get $vt) (i32.const 0) (ref.func $adder_body))
        (global.set $adder_vt (mem.readonly (local.get $vt)))))

    (local.set $cl (mem.alloc (i32.const 4) (i32.const 1)))
    (mem.write_i32 (local.get $cl) (i32.const 0) (local.get $k))
    (mem.write_ref (local.get $cl) (i32.const 0) (global.get $adder_vt))
    (mem.readonly (local.get $cl)))

  ;; Generic closure invocation: call_closure(cl, arg) -> i32
  (func $call_closure (param $cl ro_handle) (param $arg i32) (result i32)
    (call_ref (type $fn_i32_to_i32)
      (local.get $cl)                                                   ;; captured env
      (local.get $arg)                                                  ;; argument
      (mem.read_func (mem.read_ref_ro (local.get $cl) (i32.const 0)) (i32.const 0))))

  ;; Entry point: add10(5) = 15, add100(5) = 105 => 120
  (func (export "main") (result i32)
    (local $add10  ro_handle)
    (local $add100 ro_handle)
    (local.set $add10  (call $make_adder (i32.const 10)))
    (local.set $add100 (call $make_adder (i32.const 100)))
    (i32.add
      (call $call_closure (local.get $add10)  (i32.const 5))
      (call $call_closure (local.get $add100) (i32.const 5))))
)
