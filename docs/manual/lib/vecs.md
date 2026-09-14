# vecs — 성장하는 바이트 벡터

소스: `lib/vec.low` · 모듈명 `vecs` (RFC-0043 §성장, 2026-07-23)

## `vecgen` 과 무엇이 다른가 (2026-08-15)

`vecgen` 이 **두 축**(원소 타입 × 얼로케이터 타입)으로 넓어진 뒤에도 이 모듈은 남는다.
다른 것은 축이 아니라 **소유**다:

| | `vecs` | `vecgen` |
|---|---|---|
| 저장소 | **호출자가 든다** — op 에 버퍼를 넘긴다 | **컨테이너가 소유한다** |
| 성장 | 새 자리를 **값으로 돌려준다**(호출자가 재바인딩) | 안에서 갈아 끼운다 |
| 얼로케이터 | 부를 때마다 `using` 으로 건넨다(안 들고 있는다) | 필드로 **들고 있다** |

⇒ region 안에서 버퍼 수명을 **직접** 쥐고 싶을 때가 `vecs` 다. RFC-0075 Stage 3 Task 4 가
둘을 API·테스트에서 가르라고 못박은 경계가 이것이고, 그래서 축소 때 **접지 않았다**.

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 자리가 모자라면 **더 큰 자리를 받아 옮기는** 바이트 벡터다.

**언제 쓰나.** 얼마나 커질지 미리 모르는 바이트열을 모을 때 쓴다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use vecs .
use allocs .

rem 한 바이트를 민다. 자리가 모자라면 bump 에게 더 큰 자리를 받아 옮긴다.
let r option mut slice u8 . using bump be vecs.push_byte v buf 65 .
```

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 모듈은 **몇 바이트가 될지 미리 모르는 자료를 밀어 넣으며 모을 때** 쓴다. 밀어 넣다
자리가 차면 더 큰 자리를 받아 알아서 옮긴다 — 다른 언어의 "성장하는 배열"(동적 배열)에
해당하는 것의 최소형이다.

RFC-0043 은 성장(`append_grow`)을 *"범프라 재할당이 없다"* 며 없음으로 적어 뒀다. 그런데
없는 것은 **재할당**이지 **성장**이 아니다: 더 큰 자리를 받아 **옮기면** 된다. 그리고
그것은 Lowent 로 쓸 수 있다 — `reserve` 로 더 큰 자리를 받고 옛 내용을 옮긴다.

언어가 이것을 빌트인으로 넣지 않은 이유가 그것이다: **빌트인 op 증가 0.** 리프 규칙
("Lowent 로 쓸 수 있으면 라이브러리")의 세 번째 사례다. 성장 정책(두 배)도 언어가 아니라
**라이브러리의 몫**이다.

## 설계 의도와 경계

핵심 규율은 두 가지다: 몰래 할당하지 않고, 새 자리를 반환값으로 알려 준다.

- **몰래 할당하지 않는다**(RFC-0043 D1). 얼로케이터를 **`using` 절로 받는다** — `input comptime
  a type .` + `using al a .` + `requires allocs.byte_allocator a .`(RFC-0112 D8). 부르는 쪽은
  `let r … using bump be vecs.push_byte …` 로 출처를 적거나, 그 op 안에 출처가 하나뿐이면 그것이
  기본값이 된다. 얼로케이터를 안 가진 코드는 이 op 을 못 부른다. 경계가 지킨다.
- **버퍼는 값처럼 돌려준다.** 성장하면 저장소 자리가 바뀌므로 호출자가 반환된 슬라이스로
  **재바인딩**(들고 있던 변수를 새 값으로 갈아끼우기)해야 한다. 그래서
  `option (mut slice u8)` 을 돌려준다 — OOM(자리 부족)은 값이다(D5).
- **옛 자리는 안 돌아간다.** 뒷받침이 범프라 해제가 없다. 옛 버퍼는 `region` 블록이 끝날
  때 통째로 걷힌다(SPEC-004 §4.5) — 그게 이 언어의 해제다.
- 안 짓는 것: 원소 타입 일반화(지금은 `u8` 하나), pop·insert·remove, 줄어들기.
  구조는 최소다 — 길이 하나와 push 하나.

## 자료구조

벡터가 값 하나가 아니라 **두 값의 쌍**이라는 것이 이 모듈의 첫 관문이다.

```lowent
export struct vec_u8
  len u64 .
end
```

| 필드 | 타입 | 뜻 |
|---|---|---|
| `len` | `u64` | 채워진 바이트 수 |

벡터는 `vec_u8`(길이)와 `mut slice u8`(저장소) 두 값의 쌍으로 산다. 용량은 따로 안
든다 — `len buf` 가 곧 용량이다. 불변식: `v.len <= len buf` (push 가 유지한다).

## op 한눈에

op 은 둘뿐이다: 성장 정책(`next_cap`)과 밀어 넣기(`push_byte`)다.

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `next_cap` | fn | `cur u64` → `u64` (0→8, 아니면 2배) | 실패 없음 |
| `push_byte` | proc (제네릭) | (`using` 얼로케이터) `vec_u8`·버퍼·`x u8` → `option mut slice u8` | OOM 이면 `none` |

## op 상세

**매개변수를 왜 받나 — 이 모듈의 규약.** `vec_u8` 은 **길이만** 들고 있고 바이트는 호출자의
`buf` 에 산다. 그래서 밀 때마다 `v`(길이)와 `buf`(실제 자리)를 함께 넘긴다. 자리가 모자라면
얼로케이터에게 더 큰 자리를 받아 **옮겨야** 하므로 얼로케이터도 건넨다(`using`) — 언어가 뒤에서 몰래
할당하지 않는다는 뜻이고, 그래서 비용이 호출부에 보인다. 반환된 새 슬라이스를 **반드시 받아
써야** 한다(옛 `buf` 는 옮긴 뒤 죽은 자리다).


두 op 을 시그니처와 함께 본다. 매개변수마다 "왜 이것이 필요한가" 를 한 줄로 붙였다 —
특히 얼로케이터가 왜 인자로 들어오는지가 이 모듈의 요점이다.

### next_cap

다음 용량을 정하는 순수 계산이다. 성장 정책이 이 한 곳에 있다.

```lowent
export fn next_cap input cur u64 . output u64 . do
```

- `cur` — 현재 용량. 다음 용량이 지금 크기에 비례해야("두 배") 상환 O(1) 이 나오므로
  이 값이 필요하다.
- 반환: `cur == 0` 이면 8, 아니면 `cur * 2`.

### push_byte

한 바이트를 잇는다. 자리가 없으면 두 배를 받아 옮긴다 — 이 모듈의 중심 op 이다.

```lowent
export proc push_byte .
  input comptime a type .
  using al a .
  input v mut vec_u8 .
  input buf mut slice u8 . .
  input x u8 .
  output option mut slice u8 . .
  effects state via a .
  requires allocs.byte_allocator a .
```

- `a` — 얼로케이터의 **타입**(comptime). 단형화(컴파일 시점에 구체 타입으로 확정)를
  위해 타입을 따로 받는다. `byte_allocator` trait 을 충족해야 한다. 부르는 쪽은 적지 않는다 —
  `using` 에 건넨 출처의 타입에서 채워진다.
- `al` — 그 얼로케이터 actor **인스턴스**(`using` 절). 성장할 때 새 자리를 여기서 받는다 — 몰래
  할당하지 않으므로 부르는 쪽이 `using <출처>` 로 건넨다. 위치 인자가 아니다(인자 수에 안 든다).
- `effects state via a` — 비용은 얼로케이터 쪽 `reserve` 가 내는 효과를 그대로 물려받는다(D7).
- `v` — 길이를 든 벡터 머리. 어디까지 채웠는지 이것이 기억하므로 push 가 `v.len` 을 1 올린다.
- `buf` — 현재 저장소. `len buf` 가 곧 용량이라 "찼는가" 판단에 필요하다.
- `x` — 이을 바이트.
- 동작: 자리가 있으면(`v.len < len buf`) 그대로 쓰고 `some buf` 를 돌려준다. 꽉 찼으면
  `send al reserve (next_cap (len buf))` 로 **두 배를 받아** 옛 내용을 전부 복사한 뒤
  새 자리에 쓴다 — 그때는 `some <새 버퍼>` 다.
- 실패: 얼로케이터가 `none` 을 내면(OOM) `none`. 이때 `v`·`buf` 는 안 바뀐다.
- **돌려주는 슬라이스가 앞으로 쓸 자리다** — 성장했으면 새 자리, 아니면 그대로. 호출자는
  반드시 그것으로 재바인딩한다.

## 사용법과 예제

기본 흐름: 벡터 머리를 `len 0` 으로 만들고, push 할 때마다 반환된 버퍼로 재바인딩한다.

```lowent
use vecs .              rem from-생략 std 해소
use allocs .            rem 얼로케이터도 같이 쓴다
```

실제로 도는 전체 프로그램(`impl/tests/prog/vecgrow.low` — 2 바이트로 시작해 10 개를
push 하며 세 번 성장한다):

```lowent
module vecgrow .
use vecs .
use allocs .

proc main input al cap allocator . input out cap io . output u8 . effects io alloc state . do
  let memopt option mut slice u8 . be alloc_bytes al capacity 256 .   rem 뿌리에서 바이트를 받는다
  guard is_some memopt . else return 1 .
  let mem mut slice u8 . be some_value memopt .
  var bump allocs.bump_bytes be spawn actor allocs.bump_bytes . .     rem 그 위에 범프를 차린다
  var c u64 be send bump init mem .
  let b0 option mut slice u8 . be send bump reserve 2 .   rem 첫 저장소 — 일부러 2 바이트만
  guard is_some b0 . else return 2 .
  var buf mut slice u8 . be some_value b0 .
  var v vecs.vec_u8 be make vecs.vec_u8 do len 0 . end    rem 벡터 머리 — 아직 0 바이트
  var i u64 be 0 .
  while lt i 10 . do
    rem push — 찼으면 두 배를 받아 옮긴다 (2→4→8→16 으로 세 번 성장)
    let r option mut slice u8 . using bump be vecs.push_byte v buf (narrow u8 (add 65 i)) .
    guard is_some r . else return 3 .   rem OOM 이면 none — 여기서 잡는다
    set buf (some_value r) .        rem ★ 재바인딩 — 성장했으면 buf 가 새 자리다
    set i (add i 1) .
  end
  let m u64 be write_out out 1 (subslice buf 0 (field v len)) .   rem 채워진 앞부분만 출력
  return narrow u8 (field v len) .  rem 10 · 출력은 "ABCDEFGHIJ"
end
```

읽을 점: 얼로케이터는 인자 자리가 아니라 `using bump` 로 건넨다 — 타입(`allocs.bump_bytes`)은
그 출처에서 채워지고 단형화되므로 이 전달의 비용은 0 이다. 내용물 읽기는 `subslice buf 0 (field v len)` —
버퍼 전체가 아니라 채워진 앞부분만이다.

## 반례 — 이렇게 쓰면 안 된다

각 반례에 **증상**을 적었다. 첫 반례는 에러가 하나도 안 나면서 결과만 틀리는 종류라
가장 위험하다 — 그런 것부터 읽는다.

```lowent
rem ✗ 반환된 버퍼로 재바인딩하지 않는다
let r option mut slice u8 . using bump be vecs.push_byte v buf 65 .
rem set buf (some_value r) . 를 빼먹었다
let r2 option mut slice u8 . using bump be vecs.push_byte v buf 66 .
```

증상: 에러가 전혀 없다 — 컴파일도 실행도 통과하는데 **결과가 소리 없이 틀린다.** 성장이
일어난 push 뒤에 옛 `buf` 를 계속 주면, `v.len` 은 새 버퍼 기준으로 올라가 있는데 쓰기는
옛(작은) 버퍼에 간다. 데이터가 조용히 갈라진다. **반환값이 곧 다음 버퍼다** — 항상
재바인딩한다. 이 반례가 이 모듈에서 가장 위험한 실수다.

```lowent
rem ✗ OOM 을 안 본다
let r option mut slice u8 . using bump be vecs.push_byte v buf 65 .
set buf (some_value r) .
```

증상: 얼로케이터가 말랐을 때 실행 중 **E-VM-NONE 트랩(패닉)** 으로 멈춘다
(`some_value of none`). 마르기 전까지는 잘 돌기 때문에 시험에서 안 잡히기 쉽다.

```lowent
rem ✗ trait 을 충족하지 않는 값을 출처로 건넨다 (c 는 u64)
let r option mut slice u8 . using c be vecs.push_byte v buf 65 .
```

증상: **컴파일 에러 E-BOUND-UNSAT** — `u64` 는 `byte_allocator` 를 충족하지 않는다.
실행까지 가지 않는다.

```lowent
rem ✗ effects none 인 fn 에서 부른다
fn f … do
  let r option mut slice u8 . be vecs.push_byte … .
```

증상: **컴파일 에러 E-EFFECT-CALC** — `push_byte` 는 proc(`effects state`)이라
`effects none` 인 fn 안에서 못 부른다. 부르는 쪽을 proc 으로 바꾼다.

## 주의사항

성장의 비용과 "쌍" 규율에 관한 것들이다.

- **성장은 복사다.** push 한 번이 최악의 경우 O(len) 이다. 다만 두 배 정책이라 전체로는
  상환 O(1) — 가끔 비싼 복사가 있어도 push 한 번의 평균 비용은 상수라는 뜻이다. 크기를
  미리 알면 처음부터 그만큼 `reserve` 해서 시작하는 쪽이 싸다.
- **옛 버퍼는 회수되지 않는다.** 범프 위에서 n 까지 키우면 뒷받침 소비는 대략 2n(8 + 16 +
  … + n)이다. 뒷받침 용량을 정할 때 이것을 계산에 넣는다.
- `vec_u8` 와 버퍼는 쌍이다. 서로 다른 벡터의 머리와 버퍼를 섞어 넘기는 것을 막을 장치는
  없다 — 호출자 규율이다.
- 얼로케이터가 `bump_aligned` 면 성장 때마다 정렬 패딩도 소비된다.
- 오라클: 순수 slice 조작 + 범프 위 성장이라 VM 과 네이티브가 같은 바이트를 낸다.
  `vecgrow.low` 가 골든으로 그것을 잰다.
