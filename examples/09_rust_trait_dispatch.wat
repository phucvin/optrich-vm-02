;; Example 09 — Rust-Style Static Dispatch (Monomorphization)
;; Contrast with Example 06 (dynamic dispatch via vtables + `call_ref`):
;;
;; In Rust, generic trait bounds like:
;;   trait Scorable {
;;       fn base_score(&self) -> i32;
;;       fn boosted_score(&self, bonus: i32) -> i32;
;;   }
;;   fn eval<T: Scorable>(item: &T, bonus: i32) -> i32 {
;;       item.base_score() + item.boosted_score(bonus)
;;   }
;; lower via MONOMORPHIZATION to specialized direct-call functions:
;;   `$eval_Article` and `$eval_Tweet`
;;
;; Result: ZERO vtables, ZERO `call_ref`/`call_indirect`, ZERO trait wrappers —
;; only the 2 raw data blocks are allocated (verify with `--gc-stats`: live_before=2!).
;;
;; Run: ./build/optrich-vm --gc-stats examples/09_rust_trait_dispatch.wat main
(module
  ;; --- Concrete Type 1: `Article` (bytes[0..4) = words, bytes[4..8) = likes) ---
  (func $article_base (param $self ro_handle) (result i32)
    (i32.add (mem.read_i32 (local.get $self) (i32.const 0))
             (mem.read_i32 (local.get $self) (i32.const 4))))

  (func $article_boost (param $self ro_handle) (param $bonus i32) (result i32)
    (i32.add (call $article_base (local.get $self)) (local.get $bonus)))

  ;; Monomorphized `eval::<Article>` — direct `call`, zero vtable lookup!
  (func $eval_Article (param $item ro_handle) (param $bonus i32) (result i32)
    (i32.add
      (call $article_base  (local.get $item))
      (call $article_boost (local.get $item) (local.get $bonus))))

  ;; --- Concrete Type 2: `Tweet` (bytes[0..4) = retweets) ---
  (func $tweet_base (param $self ro_handle) (result i32)
    (i32.mul (mem.read_i32 (local.get $self) (i32.const 0)) (i32.const 2)))

  (func $tweet_boost (param $self ro_handle) (param $bonus i32) (result i32)
    (i32.mul (mem.read_i32 (local.get $self) (i32.const 0)) (local.get $bonus)))

  ;; Monomorphized `eval::<Tweet>` — direct `call`, zero vtable lookup!
  (func $eval_Tweet (param $item ro_handle) (param $bonus i32) (result i32)
    (i32.add
      (call $tweet_base  (local.get $item))
      (call $tweet_boost (local.get $item) (local.get $bonus))))

  (func (export "main") (result i32)
    (local $art rw_handle)
    (local $tw  rw_handle)

    ;; Article(words=20, likes=5): base=25, boost(+10)=35 => 60
    (local.set $art (mem.alloc (i32.const 8) (i32.const 0)))
    (mem.write_i32 (local.get $art) (i32.const 0) (i32.const 20))
    (mem.write_i32 (local.get $art) (i32.const 4) (i32.const 5))

    ;; Tweet(retweets=7): base=14, boost(*10)=70 => 84
    (local.set $tw (mem.alloc (i32.const 4) (i32.const 0)))
    (mem.write_i32 (local.get $tw) (i32.const 0) (i32.const 7))

    ;; Static dispatch: direct calls to monomorphized functions (zero vtables!)
    ;; Total = 60 + 84 = 144
    (i32.add
      (call $eval_Article (local.get $art) (i32.const 10))
      (call $eval_Tweet   (local.get $tw)  (i32.const 10))))
)
