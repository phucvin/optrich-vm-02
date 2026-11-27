;; Benchmark: Call Dispatch Overhead (Direct `call` vs `call_indirect` vs `call_ref` vtable)
;; Measures polymorphic interface dispatch cost in Optrich VM.
;;
;; Run: ./build/optrich-vm benchmarks/dispatch.wat direct_calls 5000000
;;      ./build/optrich-vm benchmarks/dispatch.wat indirect_calls 5000000
;;      ./build/optrich-vm benchmarks/dispatch.wat vtable_call_ref 5000000
(module
  (type $fn_sig (func (param i32) (result i32)))
  (table $tbl 2 funcref)
  (elem (i32.const 0) $step)

  (func $step (param $x i32) (result i32)
    (i32.add (local.get $x) (i32.const 1)))

  (func (export "direct_calls") (param $n i32) (result i32)
    (local $i i32)
    (local $acc i32)
    (block $done
      (loop $lp
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $acc (call $step (local.get $acc)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    (local.get $acc))

  (func (export "indirect_calls") (param $n i32) (result i32)
    (local $i i32)
    (local $acc i32)
    (block $done
      (loop $lp
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $acc (call_indirect (type $fn_sig) (local.get $acc) (i32.const 0)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    (local.get $acc))

  (func (export "vtable_call_ref") (param $n i32) (result i32)
    (local $vt rw_handle)
    (local $i i32)
    (local $acc i32)
    (local.set $vt (mem.alloc_vtable (i32.const 1)))
    (mem.write_func (local.get $vt) (i32.const 0) (ref.func $step))
    (block $done
      (loop $lp
        (br_if $done (i32.ge_s (local.get $i) (local.get $n)))
        (local.set $acc
          (call_ref (type $fn_sig) (local.get $acc) (mem.read_func (local.get $vt) (i32.const 0))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $lp)))
    (local.get $acc))
)
