# 부록 C — 흔한 실수와 고치는 법

다른 언어의 버릇으로 Lowent 를 쓸 때 자주 걸리는 자리를 모았다. 진단 코드의 뜻은 부록 B 에, 설명은 표의 장에 있다.

## <a id="sx1"></a>겉모습

| **실수** | **진단** | **고치는 법** |
|---|---|---|
| `output` 을 `input` 앞에 적는다 | `E-CLAUSE-ORDER` | 절 차례표를 따른다. 입력 밖의 절은 `--fmt` 가 옮긴다(3장) |
| `def struct p .` 처럼 블록을 점이나 줄바꿈으로 연다 | `E-STMT-NODO` | `def struct p do … end .` |
| `;` 나 `,` 로 닫는다 | `E-VOCAB-REMOVED` | 마침표 `.` |
| `p.x` 로 칸을 읽는다 | `E-FIELD-GLUED` | `field p. x .` |
| `for x in xs` | `E-VOCAB-REMOVED` | `for x xs. do … end .` |
| `let x f64 .5 .` | `E-LET-NOVALUE` | `0.5` |
| 매개변수와 같은 이름의 지역을 짓는다 | `E-NAME-SHADOW` | 새 이름을 짓는다 |
| `count`·`text`·`len` 같은 이름을 쓴다 | `E-NAME-BUILTIN` 따위 | 기본 연산·텍스트 리터럴 낱말은 이름이 될 수 없다 |
| 모듈 이름과 op 이름이 같다 | `E-NAME-DUP` | 모듈 이름을 바꾼다 |
| `add a b .` 처럼 변수에 점을 붙이지 않는다 | `E-DOT-MISSING` | `add a. b. .` — 변수는 `a.` 로 적는다(3장) |
| `let x be u8 4 .` | `E-LET-BE` | `let x u8 4 .` |
| `return add a. b. .` 처럼 점이 하나 모자란다 · `end` 뒤의 점을 빠뜨린다 | `E-DOT-MISSING` | `return add a. b. . .` · `end .` — 연 만큼 닫는다 |
| `if gt a. 3 . . do … end .` 처럼 점을 하나 더 찍는다 | `E-CLOSER-EXTRA` | `if gt a. 3 . do … end .` |
| `for i count u64 n . do` | `E-FOR-OLD` | `repeat i u64 n. do` |

*표 50.1 — 겉모습에서 걸리는 실수*

## <a id="sx2"></a>값과 흐름

| **실수** | **진단** | **고치는 법** |
|---|---|---|
| `let` 에 `set` | `E-IMMUTABLE` | `var` 로 짓는다 |
| 정수를 조건에 쓴다 | `E-TYPE-COND` | `gt n. 0 .` 처럼 묻는 것을 적는다 |
| 넘침을 C 처럼 감길 것이라 기대한다 | `E-VM-OVERFLOW`(실행 중) | `wrap_add`·`sat_add`·`chk_add` 로 처분을 고른다 |
| `narrow u8 x. .` 가 잘라 줄 것이라 기대한다 | `E-VM-CAST`(실행 중) | `narrow_wrap`·`narrow_sat`·`narrow_try` |
| `guard` 의 `else` 가 떠나지 않는다 | `E-GUARD-FALLTHROUGH` | `return`·`break`·`continue`·`panic`, 아니면 `if` |
| 어떤 길에서 `return` 이 없다 | `E-RETURN-PARTIAL` | 모든 길에서 돌려준다 |
| `if` 를 값으로 쓴다 | `E-IF-VALUE` | `var` 를 짓고 갈래마다 `set` |
| `match` 에서 경우를 빠뜨린다 | `E-MATCH-INEXHAUSTIVE` | 빠진 `case` 나 `case _` |
| 열거의 갈래를 점 없이 줄마다 적는다 | `E-ENUM-DOT` | 갈래마다 `red .` |
| 확인 없이 `some_value` 로 꺼낸다 | `E-VM-NONE`(실행 중) | `guard is_some` · `value_or` · `match` |
| `array 4 u8` | `E-TYPE-ARRAY` | `array u8 4` |

*표 50.2 — 값과 흐름에서 걸리는 실수*

## <a id="sx3"></a>효과·권한·메모리

| **실수** | **진단** | **고치는 법** |
|---|---|---|
| `fn` 에서 출력한다 | `E-EFFECT-CALC` | `proc` 과 `effects io` |
| `fn` 에 `effects none` 을 적는다 | `E-EFFECT-REDUNDANT` | 절을 지운다 |
| `fn` 이 `mut` 매개변수로 호출자에게 쓴다 | `E-EFFECT-PURITY` | `proc` 으로 |
| `effects io` 인데 권한을 받지 않았다 | `E-EFFECT-NO-CAP` | `input out cap io .` |
| 권한을 받아 두고 기본 연산에 넘기지 않는다 | `E-CAP-MISSING` | `write_out out. 1 …` 처럼 첫 피연산자로 |
| 시작점이 데이터를 받는다 | `E-ENTRY-PARAMS` | 시작점은 권한만 받는다. 인자는 `cap args` |
| `mut` 없는 슬라이스에 쓴다 | `E-TYPE-MUT` | `mut slice` |
| `ref` 로 쓴다 | `E-TYPE-REF` | `mut_ref` |
| 같은 값을 쓰기로 두 번 빌린다 | `E-EXCL` | 빌림을 나누거나 차례를 나눈다 |
| 지역의 참조를 돌려준다 | `E-ESCAPE` | 값을 돌려주거나 호출자의 저장소에 담는다 |
| `mut ref slice` | `E-MREF-SLICE` | 새 슬라이스를 돌려준다 |
| 영역의 바이트를 밖으로 들고 나간다 | `E-REGION-ESCAPE` | 영역을 넓히거나 값만 들고 나간다 |
| 파일·버퍼를 닫지 않는다 | `E-OWN-INCOMPLETE` | `close`·`finish` 를 부른다. 버리려면 `drop` |
| 옮긴 값을 다시 쓴다 | `E-OWN-MOVED` | 넘기기 전에 쓰거나 돌려받는다 |
| 운영체제 없는 대상에서 힙을 쓴다 | `E-HEAP-NOHOST` | 고정 창(`cap allocator`)과 `alloc` |

*표 50.3 — 효과·권한·메모리에서 걸리는 실수*

## <a id="sx4"></a>추상과 동시성

| **실수** | **진단** | **고치는 법** |
|---|---|---|
| 트레이트 서명에 `fn`·`proc` 을 적는다 | `E-TRAIT-SIG` | `area input s self . output u64 .` |
| `proc` 에 효과 줄을 안 적는다 | `E-EFFECT-MISSING` | `effects <원자> .` 를 적는다 — 없으면 `effects none .`(`--doc` 의 `inferred:` 가 답이다) |
| 타입 인자가 트레이트를 갖추지 않았다 | `E-BOUND-UNSAT` | `satisfies` 와 op 을 더한다 |
| `comptime` 자리에 실행 값을 준다 | `E-COMPTIME-ARG` | 리터럴이나 모듈 상수 |
| `task_group` 밖에서 `spawn <op>` | `E-SPAWN-SCOPE` | `task_group do … end .` 로 감싼다 |
| 나누는 되풀이에서 누적 변수에 쓴다 | `E-PAR-CARRY` | `reduce <자리> <연산> .` |
| `pipe` 에 정렬 같은 스테이지를 쓴다 | `E-PIPE-STAGE` | 스테이지 일곱과 종결자 다섯만 쓴다 |
| 효과 있는 붙은 op 을 `fn` 안에서 `method` 로 부른다 | (이 판에서는 거절되지 않는다) | 직접 `<타입>.<이름>` 으로 부른다(23장) |

*표 50.4 — 추상과 동시성에서 걸리는 실수*

---

[← 이전](sec54.md) · [목차로](README.md) · [다음 →](sec56.md)
