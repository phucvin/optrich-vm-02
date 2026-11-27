;; Guessed WAT for examples/optic/06_binary_buffers_and_bits.opc
;; ByteBuf: mem.alloc(8, 1) -> bytes[0..4)=len, bytes[4..8)=cap, refs[0]=data ([]u8)
(module
  (import "host" "trace_i32" (func $trace_i32 (param ro_handle i32)))
  (import "host" "trace_str" (func $trace_str (param ro_handle ro_handle)))

  (string $l1 "step1.pkt_magic")
  (string $l2 "step2.pkt_seq")
  (string $l3 "step3.pkt_flags")
  (string $l4 "step4.pkt_payload")
  (string $l5 "step5.built_str")
  (string $l6 "step6.utf8_bytes")
  (string $l7 "step7.utf8_chars")
  (string $l8 "step8.bits_sum")
  (string $l9 "step9.fnv1a_low16")

  (string $s_ok    "OK")
  (string $s_seq   "seq=")
  (string $s_colon ":ok")
  (string $s_optic "optic")

  ;; Big-endian helpers
  (func $write_u16_be (param $buf rw_handle) (param $off i32) (param $v i32)
    (mem.write_u8 (local.get $buf) (local.get $off) (i32.and (i32.shr_u (local.get $v) (i32.const 8)) (i32.const 255)))
    (mem.write_u8 (local.get $buf) (i32.add (local.get $off) (i32.const 1)) (i32.and (local.get $v) (i32.const 255))))

  (func $read_u16_be (param $buf ro_handle) (param $off i32) (result i32)
    (i32.or
      (i32.shl (mem.read_u8 (local.get $buf) (local.get $off)) (i32.const 8))
      (mem.read_u8 (local.get $buf) (i32.add (local.get $off) (i32.const 1)))))

  (func $write_u32_be (param $buf rw_handle) (param $off i32) (param $v i32)
    (mem.write_u8 (local.get $buf) (local.get $off) (i32.and (i32.shr_u (local.get $v) (i32.const 24)) (i32.const 255)))
    (mem.write_u8 (local.get $buf) (i32.add (local.get $off) (i32.const 1)) (i32.and (i32.shr_u (local.get $v) (i32.const 16)) (i32.const 255)))
    (mem.write_u8 (local.get $buf) (i32.add (local.get $off) (i32.const 2)) (i32.and (i32.shr_u (local.get $v) (i32.const 8)) (i32.const 255)))
    (mem.write_u8 (local.get $buf) (i32.add (local.get $off) (i32.const 3)) (i32.and (local.get $v) (i32.const 255))))

  (func $read_u32_be (param $buf ro_handle) (param $off i32) (result i32)
    (i32.or
      (i32.or
        (i32.shl (mem.read_u8 (local.get $buf) (local.get $off)) (i32.const 24))
        (i32.shl (mem.read_u8 (local.get $buf) (i32.add (local.get $off) (i32.const 1))) (i32.const 16)))
      (i32.or
        (i32.shl (mem.read_u8 (local.get $buf) (i32.add (local.get $off) (i32.const 2))) (i32.const 8))
        (mem.read_u8 (local.get $buf) (i32.add (local.get $off) (i32.const 3))))))

  ;; ByteBuf / StringBuilder
  (func $ByteBuf__new (param $cap i32) (result rw_handle)
    (local $b rw_handle)
    (local.set $b (mem.alloc (i32.const 8) (i32.const 1)))
    (mem.write_i32 (local.get $b) (i32.const 0) (i32.const 0))
    (mem.write_i32 (local.get $b) (i32.const 4) (local.get $cap))
    (mem.write_ref (local.get $b) (i32.const 0) (mem.alloc (local.get $cap) (i32.const 0)))
    (local.get $b))

  (func $ByteBuf__push_str (param $self rw_handle) (param $s ro_handle)
    (local $len i32)
    (local $slen i32)
    (local.set $len (mem.read_i32 (local.get $self) (i32.const 0)))
    (local.set $slen (mem.byte_len (local.get $s)))
    (mem.copy (mem.read_ref (local.get $self) (i32.const 0)) (local.get $len)
              (local.get $s) (i32.const 0) (local.get $slen))
    (mem.write_i32 (local.get $self) (i32.const 0) (i32.add (local.get $len) (local.get $slen))))

  (func $ByteBuf__push_u32_dec (param $self rw_handle) (param $n i32)
    (local $tmp rw_handle)
    (local $k i32)
    (local $i i32)
    (local $len i32)
    (local $data handle)
    (local.set $tmp (mem.alloc (i32.const 10) (i32.const 0)))
    (if (i32.eqz (local.get $n))
      (then
        (mem.write_u8 (local.get $tmp) (i32.const 0) (i32.const 48))
        (local.set $k (i32.const 1)))
      (else
        (block $done
          (loop $lp
            (br_if $done (i32.eqz (local.get $n)))
            (mem.write_u8 (local.get $tmp) (local.get $k)
              (i32.add (i32.const 48) (i32.rem_u (local.get $n) (i32.const 10))))
            (local.set $n (i32.div_u (local.get $n) (i32.const 10)))
            (local.set $k (i32.add (local.get $k) (i32.const 1)))
            (br $lp)))))
    (local.set $len (mem.read_i32 (local.get $self) (i32.const 0)))
    (local.set $data (mem.read_ref (local.get $self) (i32.const 0)))
    (local.set $i (i32.const 0))
    (block $b2
      (loop $l2
        (br_if $b2 (i32.ge_s (local.get $i) (local.get $k)))
        (mem.write_u8 (local.get $data) (i32.add (local.get $len) (local.get $i))
          (mem.read_u8 (local.get $tmp) (i32.sub (i32.sub (local.get $k) (i32.const 1)) (local.get $i))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $l2)))
    (mem.write_i32 (local.get $self) (i32.const 0) (i32.add (local.get $len) (local.get $k))))

  (func $ByteBuf__as_str (param $self ro_handle) (result ro_handle)
    (mem.readonly
      (mem.span (mem.read_ref (local.get $self) (i32.const 0))
                (i32.const 0)
                (mem.read_i32 (local.get $self) (i32.const 0)))))

  ;; UTF-8 codepoint counter
  (func $utf8_count_codepoints (param $s ro_handle) (result i32)
    (local $count i32)
    (local $i i32)
    (local $end i32)
    (local.set $end (mem.byte_len (local.get $s)))
    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $end)))
        (if (i32.ne (i32.and (mem.read_u8 (local.get $s) (local.get $i)) (i32.const 0xC0)) (i32.const 0x80))
          (then (local.set $count (i32.add (local.get $count) (i32.const 1)))))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))
    (local.get $count))

  ;; FNV-1a 32-bit hash (wrapping u32 arithmetic)
  (func $fnv1a_32 (param $data ro_handle) (result i32)
    (local $h i32)
    (local $i i32)
    (local $end i32)
    (local.set $h (i32.const -2128831035)) ;; 0x811c9dc5 = 2166136261u
    (local.set $end (mem.byte_len (local.get $data)))
    (block $break
      (loop $cont
        (br_if $break (i32.ge_s (local.get $i) (local.get $end)))
        (local.set $h (i32.mul (i32.xor (local.get $h) (mem.read_u8 (local.get $data) (local.get $i)))
                               (i32.const 16777619)))
        (local.set $i (i32.add (local.get $i) (i32.const 1)))
        (br $cont)))
    (local.get $h))

  (func (export "main") (result i32)
    (local $pkt rw_handle)
    (local $ro_pkt ro_handle)
    (local $magic i32)
    (local $seq i32)
    (local $flags i32)
    (local $payload ro_handle)
    (local $sb rw_handle)
    (local $utf8_buf rw_handle)
    (local $utf8_s ro_handle)
    (local $x i32)
    (local $bits_sum i32)
    (local $h i32)

    ;; 1. Build & parse binary packet
    (local.set $pkt (mem.alloc (i32.const 12) (i32.const 0)))
    (call $write_u16_be (local.get $pkt) (i32.const 0) (i32.const 0x4F50))
    (call $write_u32_be (local.get $pkt) (i32.const 2) (i32.const 0x01020304))
    (mem.write_i32 (local.get $pkt) (i32.const 6) (i32.const 777))
    (mem.copy (local.get $pkt) (i32.const 10) (mem.const_str $s_ok) (i32.const 0) (i32.const 2))

    (local.set $ro_pkt (mem.readonly (local.get $pkt)))
    (local.set $magic (call $read_u16_be (local.get $ro_pkt) (i32.const 0)))
    (local.set $seq (call $read_u32_be (local.get $ro_pkt) (i32.const 2)))
    (local.set $flags (mem.read_i32 (local.get $ro_pkt) (i32.const 6)))
    (local.set $payload (mem.span (local.get $ro_pkt) (i32.const 10) (i32.const 2)))

    (call $trace_i32 (mem.const_str $l1) (local.get $magic))
    (call $trace_i32 (mem.const_str $l2) (local.get $seq))
    (call $trace_i32 (mem.const_str $l3) (local.get $flags))
    (call $trace_str (mem.const_str $l4) (local.get $payload))

    ;; 2. ByteBuf / StringBuilder
    (local.set $sb (call $ByteBuf__new (i32.const 64)))
    (call $ByteBuf__push_str (local.get $sb) (mem.const_str $s_seq))
    (call $ByteBuf__push_u32_dec (local.get $sb) (i32.const 420))
    (call $ByteBuf__push_str (local.get $sb) (mem.const_str $s_colon))
    (call $trace_str (mem.const_str $l5) (call $ByteBuf__as_str (mem.readonly (local.get $sb))))

    ;; 3. UTF-8 string "Hi\xe2\x82\xac!" (6 bytes, 4 codepoints)
    (local.set $utf8_buf (mem.alloc (i32.const 6) (i32.const 0)))
    (mem.write_u8 (local.get $utf8_buf) (i32.const 0) (i32.const 72))   ;; 'H'
    (mem.write_u8 (local.get $utf8_buf) (i32.const 1) (i32.const 105))  ;; 'i'
    (mem.write_u8 (local.get $utf8_buf) (i32.const 2) (i32.const 0xE2)) ;; Euro byte 0
    (mem.write_u8 (local.get $utf8_buf) (i32.const 3) (i32.const 0x82)) ;; Euro byte 1
    (mem.write_u8 (local.get $utf8_buf) (i32.const 4) (i32.const 0xAC)) ;; Euro byte 2
    (mem.write_u8 (local.get $utf8_buf) (i32.const 5) (i32.const 33))   ;; '!'
    (local.set $utf8_s (mem.readonly (local.get $utf8_buf)))
    (call $trace_i32 (mem.const_str $l6) (mem.byte_len (local.get $utf8_s)))
    (call $trace_i32 (mem.const_str $l7) (call $utf8_count_codepoints (local.get $utf8_s)))

    ;; 4. Bit manipulation (clz + ctz + popcnt) + FNV-1a
    (local.set $x (i32.const 0x00100500))
    (local.set $bits_sum
      (i32.add (i32.add (i32.clz (local.get $x)) (i32.ctz (local.get $x)))
               (i32.popcnt (local.get $x))))
    (call $trace_i32 (mem.const_str $l8) (local.get $bits_sum))

    (local.set $h (call $fnv1a_32 (mem.const_str $s_optic)))
    (call $trace_i32 (mem.const_str $l9) (i32.and (local.get $h) (i32.const 0xFFFF)))

    (i32.add (local.get $flags) (local.get $bits_sum)))
)
