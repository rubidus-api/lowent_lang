# 부록 A — 낱말과 내장 연산

낱말을 **설명**하는 곳은 본문이다. 이 부록은 코드를 읽다 막혔을 때 훑어볼 표만 모은다. 낱말 목록은 명세의 부록 A 와 같고, 그 목록에 없는 것은 낱말이 아니다.

## <a id="sx1"></a>낱말 마흔셋

| **갈래** | **낱말** | **다루는 장** |
|---|---|---|
| 선언 | `module` `use` `type` `newtype` `struct` `enum` `trait` `contract` `actor` `state` | 21·13장 |
| op | `fn` `proc` `export` `unsafe` `extern` `satisfies` | 5장 |
| 지역 | `let` `var` `set` `be` | 6장 |
| 흐름 | `if` `else` `while` `for` `guard` `match` `case` `return` `break` `continue` `try` | 7장 |
| 식과 값 | `expr` `make` `true` `false` `none` | 8장 |
| 동시성 | `spawn` `send` | 25장 |
| 소유 | `drop` | 19장 |
| 시험 | `test` `expect` | 31장 |
| 블록 | `do` `end` | 3장 |

*표 50.1 — 낱말의 전부*

`rem` 과 `note` 는 주석을 여는 표시이고, `input`·`output`·`effects`·`requires` 같은 절 머리와 `add`·`len` 같은 내장 연산은 낱말이 아니지만 이름으로 쓸 수 없다.

## <a id="sx2"></a>없앤 낱말

| **없앤 것** | **대신** |
|---|---|
| `loop` | `while true .` |
| `give` | `return` |
| `unit` | `void` |
| `calcop` · `procop` | `fn` · `proc` |
| `is` · `as`(선언 속) · `local` | 적지 않는다 |
| `to` · `in`(칸 접근) | `field a b` · `index a i` |
| `on` | 액터 안의 `proc` |
| `fail` | `return error <갈래>` |
| `;` · `,` | `.` |

*표 50.2 — 없앤 낱말과 대신 쓰는 것(`E-VOCAB-REMOVED`)*

`as` 와 `to` 는 `use … as <별칭>` 과 `case <아래> to <위>` 의 자리 표식으로만 남아 있다.

## <a id="sx3"></a>자주 쓰는 내장 연산

| **갈래** | **연산** |
|---|---|
| 산술(멈춤) | `add` `sub` `mul` `div` `mod` `neg` `abs` `min` `max` |
| 산술(처분을 고름) | `wrap_*` `sat_*` `chk_*` `div_nz` `nonzero_of` |
| 폭 바꾸기 | `widen` `narrow` `narrow_wrap` `narrow_sat` `narrow_try` `cast` `bit_cast` |
| 비교와 논리 | `eq` `ne` `lt` `le` `gt` `ge` `and` `or` `not` |
| 비트 | `bit_and` `bit_or` `bit_xor` `bit_not` `shl` `shr` `rotl` `rotr` `wrap_shl` `wrap_shr` `count_ones` `leading_zeros` `trailing_zeros` `byte_swap` |
| 줄 | `len` `index` `subslice` `view` `view_array` `same_slice` |
| 묶음 | `field` `method` `isa` `get` |
| 답을 담는 타입 | `some` `ok` `error` `is_some` `is_none` `is_ok` `is_error` `some_value` `ok_value` `error_value` `value_or` |
| 번역 시점 | `size_of` `comptime` `config` |
| 할당 | `alloc_bytes` |
| 입출력 잎 | `write_out` `read_in` `arg` · 파일·연결·시계·난수·터미널 잎은 표준 라이브러리가 감싼다 |
| 원자 | `atomic_load` `atomic_store` `atomic_add` `atomic_sub` `atomic_swap` `atomic_cas` `atomic_fence` |
| 장치 | `read_volatile` `write_volatile` |
| 동시성 | `await` `drain` `channel` `chsend` `chrecv` `yield` |

*표 50.3 — 내장 연산의 갈래*

전체 목록과 뜻은 저장소의 `docs/spec/BUILTIN-MEANINGS.tsv` 가 정본이다.

## <a id="sx4"></a>효과와 권한

| **효과** | **허락하는 권한** | **다루는 장** |
|---|---|---|
| `io` | `cap io` · `file_system` · `net` · `tty` · `clock` · `random` | 16장 |
| `alloc` · `heap` | `cap allocator` · `cap heap` | 18·20장 |
| `atomic` | `cap atomic` | 27장 |
| `device` | `cap mmio` | 30장 |
| `unsafe` | `cap c` · `cap machine`(쓰는 자리에 따라) | 29·30장 |
| `state` · `panic` · `wait` · `concurrent` | 권한 없이 적는다 | 15·26장 |

*표 50.4 — 효과 원자와 짝이 되는 권한*

---

[← 이전](ch50.md) · [목차로](README.md) · [다음 →](sec54.md)
