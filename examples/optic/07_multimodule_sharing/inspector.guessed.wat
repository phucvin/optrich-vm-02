;; Guessed WAT for examples/optic/07_multimodule_sharing/inspector.opc
(module
  (global $CALL_COUNT (mut i32) (i32.const 0))

  (func (export "inspect_checksum") (param $data ro_handle) (result i32)
    (local $sum i32)
    (local $i i32)
    (local $end i32)
    (global.set $CALL_COUNT (i32.add (global.get $CALL_COUNT) (i32.const 1)))
    (local.set $end (mem.byte_len (local.get $data)))
    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $end)))
        (local.set $sum (i32.add (local.get $sum) (mem.read_u8 (local.get $data) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))
    (local.get $sum))

  (func (export "get_call_count") (result i32)
    (global.get $CALL_COUNT))
)
