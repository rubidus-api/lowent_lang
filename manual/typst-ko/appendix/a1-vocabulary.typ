#import "../lib.typ": *

= 부록 A --- 낱말과 기본 연산

낱말을 *설명*하는 곳은 본문이다. 이 부록은 코드를 읽다 막혔을 때 훑어볼 표만 모은다. 낱말 목록은 명세의 부록 A 와 같고, 그 목록에
없는 것은 낱말이 아니다.

== 낱말 마흔넷

#dtable(
  columns: 3,
  id: "a1-keywords",
  caption: [낱말의 전부],
  [*갈래*], [*낱말*], [*다루는 장*],
  [선언], [`module` `use` `def` `type` `newtype` `struct` `enum` `trait` `contract` `actor` `state`], [#chrefs("modules", "named-types")],
  [op], [`fn` `proc` `export` `unsafe` `extern` `satisfies`], [#chref("ops")],
  [지역], [`let` `var` `set` `keep`], [#chref("locals")],
  [흐름], [`if` `else` `while` `for` `repeat` `range` `cycle` `next` `step` `guard` `match` `case` `return` `break` `continue` `try`], [#chref("control")],
  [식과 값], [`expr` `lit` `true` `false` `none`], [#chref("expr")],
  [동시성], [`spawn` `send`], [#chref("actors")],
  [소유], [`drop`], [#chref("ownership")],
  [시험], [`test` `expect`], [#chref("build-test")],
  [블록], [`do` `end`], [#chref("surface")],
)

`rem` 과 `note` 는 주석을 여는 표시이고, `input`·`output`·`effects`·`requires` 같은 절 머리와 `add`·`len` 같은 기본 연산은 낱말이
아니지만 이름으로 쓸 수 없다.

== 없앤 낱말

#dtable(
  columns: 2,
  id: "a1-removed",
  caption: [없앤 낱말과 대신 쓰는 것(`E-VOCAB-REMOVED`)],
  [*없앤 것*], [*대신*],
  [`loop`], [`while true`],
  [`give`], [`return`],
  [`unit`], [`void`],
  [`calcop` · `procop`], [`fn` · `proc`],
  [`is` · `as`(선언 속) · `local`], [적지 않는다],
  [`to` · `in`(칸 접근)], [`field a. b .` · `idx a. i. .`],
  [`on`], [액터 안의 `proc`],
  [`fail`], [`return error <갈래>`],
  [`;` · `,`], [`.`],
  [`make`], [`lit`],
  [`index`], [`idx`],
  [`def` 없는 `struct`·`enum`·`type`·`newtype` 선언], [`def struct` · `def enum` · `def type` · `def newtype`],
)

`as` 와 `to` 는 `use … as <별칭>` 과 `case <아래> to <위>` 의 자리 표식으로만 남아 있다.

== 자주 쓰는 기본 연산

#dtable(
  columns: 2,
  id: "a1-builtins",
  caption: [기본 연산의 갈래],
  [*갈래*], [*연산*],
  [산술(멈춤)], [`add` `sub` `mul` `div` `mod` `neg` `abs` `min` `max`],
  [산술(처분을 고름)], [`wrap_*` `sat_*` `chk_*` `div_nz` `nonzero_of`],
  [폭 바꾸기], [`widen` `narrow` `narrow_wrap` `narrow_sat` `narrow_try` `cast` `bit_cast`],
  [비교와 논리], [`eq` `ne` `lt` `le` `gt` `ge` `and` `or` `not`],
  [비트], [`bit_and` `bit_or` `bit_xor` `bit_not` `shl` `shr` `rotl` `rotr` `wrap_shl` `wrap_shr` `count_ones` `leading_zeros` `trailing_zeros` `byte_swap` `clmul_lo` `clmul_hi`],
  [줄], [`len` `idx` `subslice` `view` `view_array` `same_slice`],
  [묶음], [`field` `method` `isa` `payload`],
  [답을 담는 타입], [`some` `ok` `error` `is_some` `is_none` `is_ok` `is_error` `some_value` `ok_value` `error_value` `value_or`],
  [번역 시점], [`size_of` `comptime` `config`],
  [할당], [`alloc_bytes`],
  [입출력 잎], [`write_out` `read_in` `arg` · 파일·연결·시계·난수·터미널 잎은 표준 라이브러리가 감싼다],
  [원자], [`atomic_load` `atomic_store` `atomic_add` `atomic_sub` `atomic_swap` `atomic_cas` `atomic_fence`],
  [장치], [`read_volatile` `write_volatile`],
  [동시성], [`await` `drain` `channel` `chsend` `chrecv` `yield` `spawn` `send`],
  [원자(비트)], [`atomic_and` `atomic_or` `atomic_xor`],
  [부동소수 수학 --- #chref("numbers")], [`sqrt` `sin` `cos` `exp` `log` `pow` `floor` `ceil` `round` `fmod` `sum_neumaier` `sum_seq`],
  [파이프 단계 --- #chref("pipe")], [`pipe` `map` `filter` `fold` `scan` `take` `skip` `zip` `enumerate` `collect` `count` `all` `any` `into`],
  [레인(SIMD) --- #chref("parallel-atomic")], [`splat` `load` `store` `load_masked` `store_masked` `lane_select` `lane_any` `lane_all` `reduce_add` `reduce_mul` `reduce_min` `reduce_max` `lane_avg` `native_lanes` `lane_reverse` `lane_rotate` `shuffle` `prefetch`],
  [작은 수의 집합 --- #chref("named-types")], [`bitset_new` `bitset_union` `bitset_intersect` `bitset_difference` `bitset_complement` `bitset_contains` `bitset_is_subset` `bitset_is_empty` `bitset_remove`],
  [배치와 뷰 --- #chref("named-types")], [`encode` `try_view` `view_segments` `seg` `segs` `capacity`],
  [빌림과 영역 --- #chref("references") · #chref("regions")], [`ref` `mut_ref` `deref` `borrow` `region` `stack_new` `push` `pop` `swap`],
  [계약과 오류 --- #chref("contracts")], [`range` `ret` `expect` `panic`],
  [빌트인 연산(계산) --- `call_builtin` 뒤에서만 선다], [`clmul_lo` `clmul_hi` `aes_round` `aes_round_last` `aes_ctr` `ghash` `chacha20` `poly1305` `aes_gcm` `chacha_poly` `sha256` `sha384` `sha512` `crc32` `hash_bytes` `rng_next`],
  [C 문자열 --- #chref("ffi")], [`cstr_of` `str_from_cstr`],
  [빌트인 연산(파일 · 그물 · 프로세스) --- `call_builtin` 뒤에서만 선다], [`file_open` `file_read` `file_write` `file_seek` `file_close` `file_type` `link_type` `dir_open` `dir_read` `dir_close` `dir_make` `path_remove` `path_rename` `net_listen` `net_accept` `net_connect` `net_resolve` `net_send` `net_recv` `net_close` `net_pair` `net_port`],
  [호스트 잎 --- #chref("lib-map")], [`env_get` `reactor_new` `r_read` `r_write` `random_bytes` `time_local` `time_sleep`],
)

★ *빌트인 연산은 제 이름공간을 갖는다.* `call_builtin sha256 msg. out. .` 처럼 그 자리에서만 서고, 저자는 같은 철자를 제 이름으로 지을 수 있다. 저자의 이름 가운데 없는 것을 맨 이름으로 부르면 `E-BUILTIN-BARE`, `call_builtin` 뒤에 모르는 이름을 대면 `E-BUILTIN-NAME` 이다. 빌트인 연산은 시스템 라이브러리를 짓는 재료이고, 프로그램은 그것을 감싼 모듈을 부른다(#chref("lib-map")).

이 표는 정본의 기본 연산을 빠짐없이 무리로 나눈 것이다. 무리 이름 옆의 장이 그 무리를 예제와 함께 설명한다. 한 줄씩의 뜻은 저장소의
`docs/spec/BUILTIN-MEANINGS.tsv` 가 정본이다.

== 효과와 권한

#dtable(
  columns: 3,
  id: "a1-effects",
  caption: [효과 원자와 짝이 되는 권한],
  [*효과*], [*허락하는 권한*], [*다루는 장*],
  [`io`], [`cap io` · `file_system` · `net` · `tty` · `clock` · `random`], [#chref("capabilities")],
  [`alloc` · `heap`], [`cap allocator` · `cap heap`], [#chrefs("regions", "fixed-memory")],
  [`atomic`], [`cap atomic`], [#chref("parallel-atomic")],
  [`device`], [`cap mmio`], [#chref("hardware")],
  [`unsafe`], [`cap c` · `cap machine`(쓰는 자리에 따라)], [#chrefs("ffi", "hardware")],
  [`state` · `panic` · `wait` · `concurrent`], [권한 없이 적는다], [#chrefs("effects", "tasks-channels")],
)
