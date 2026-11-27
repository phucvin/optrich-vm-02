;; Guessed WAT for examples/optic/07_multimodule_sharing/producer.opc
(module
  (global $HEADER_TAG i32 (i32.const 10))
  (global $NEXT_SEQ (mut i32) (i32.const 100))

  (func (export "make_packet") (param $payload_val i32) (result rw_handle)
    (local $seq i32)
    (local $buf rw_handle)
    (local.set $seq (global.get $NEXT_SEQ))
    (global.set $NEXT_SEQ (i32.add (local.get $seq) (i32.const 1)))
    (local.set $buf (mem.alloc (i32.const 3) (i32.const 0)))
    (mem.write_u8 (local.get $buf) (i32.const 0) (global.get $HEADER_TAG))
    (mem.write_u8 (local.get $buf) (i32.const 1) (i32.and (local.get $seq) (i32.const 255)))
    (mem.write_u8 (local.get $buf) (i32.const 2) (local.get $payload_val))
    (local.get $buf))
)
