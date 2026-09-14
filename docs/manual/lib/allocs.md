# allocs — 빌린 바이트를 잘라 주는 범프 얼로케이터

소스: `lib/alloc.low` · 모듈명 `allocs` (RFC-0043 A3 · RFC-0068 S5)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 빌린 큰 바이트 덩어리를 **앞에서부터 잘라 주는** 얼로케이터다.

**언제 쓰나.** 여러 조각의 임시 메모리가 필요할 때 쓴다. 개별 반납은 **마지막 조각 하나**뿐이다 — 나머지는 통째로 버린다.

> **2026-09-13 바뀐 것 (RFC-0112).** ① `init` 은 trait 에서 빠졌다 — 빌린 바이트를 받는 범프만의 일이다.
> ② `grow` 는 크기 대신 **늘릴 조각 자체**를 받는다(`same_slice` 로 정체를 확인). ③ `freeing_allocator` 와
> `release` 가 생겼다. ④ 뿌리를 딛는 기본 얼로케이터 `fixed_bytes`·`heap_bytes` 가 같은 trait 으로 선다.
> ⑤ 얼로케이터를 받는 op 은 `using al a .` 절로 받는다(부르는 쪽은 `let x … using b be …`).

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use allocs .

var bump allocs.bump_bytes be spawn actor allocs.bump_bytes . .
var c u64 be send bump init mem .           rem 잘라 줄 원본 바이트를 준다
let b option mut slice u8 . be send bump reserve 64 .   rem 64 바이트를 잘라 받는다
```

**읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 모듈은 **큰 버퍼 하나를 미리 받아 두고, 필요할 때마다 앞에서부터 조금씩 잘라 쓰고
싶을 때** 쓴다. "만들 때는 여럿, 버릴 때는 한꺼번에" 인 자료(파서의 임시 노드, 한 처리
동안만 사는 문자열 등)에 맞는 도구다.

**범프(bump) 얼로케이터**란 가장 단순한 할당기다: 커서(다음 빈 자리 표시) 하나를 두고,
달라는 만큼 잘라 준 뒤 커서를 그만큼 앞으로 "민다(bump)". 개별 해제가 없어서 빠르고
단순하다 — 되돌리는 일은 처음부터 안 한다.

Lowent 에는 암묵 전역 힙이 없다. 바이트가 프로그램에 **처음 들어오는** 자리는 뿌리 op
`alloc_bytes <권한> capacity n` 하나뿐이고, 뿌리는 둘이다(RFC-0112 D2·D3): **고정 창**
(`cap allocator` · 효과 `alloc` — 베어메탈에서는 링커가 창의 경계를 정한다)과 **힙**(`cap heap` · 효과
`heap` — 운영체제가 있는 호스티드에만 있다). 어느 쪽이든 권한이 필요하다(RFC-0043 D1). `allocs` 는 그
**뿌리 위**를 맡는다: `slice u8` 을 받아 잘라 주는 **평범한 actor** 들이다. 받은 바이트가 뿌리에서 왔든
호출자가 빌려준 것이든 묻지 않는다. 뿌리에서 곧장 깎는 기본 얼로케이터(`fixed_bytes`·`heap_bytes`)도
같은 trait 을 갖춘 평범한 actor 다 — 아래 «기본 얼로케이터» 를 보라.

언어가 이것을 빌트인으로 넣지 않은 이유는 리프 규칙이다 — actor·trait·option·subslice·guard
등 **언어가 이미 가진 것만으로 쓸 수 있으므로** 라이브러리다. 빌트인 op 증가는 0 이다.
그리고 이 분할 덕분에 `cap allocator` 를 안 받은 코드도 **남이 준 바이트 위에서는 온전히
할당한다** — 권한이 능력을 가른다(RFC-0068 F4(c)).

## 설계 의도와 경계

이 절은 "왜 이런 모양인가" 와 "무엇을 일부러 안 하는가" 를 적는다. 경계를 알면 이 모듈을
어디까지 믿어도 되는지가 보인다.

- **얼로케이터는 갈아끼울 수 있다.** 라이브러리 코드는 `input comptime a type .` + `using al a .` +
  `requires byte_allocator a .` 로 어느 구현이든 받는다(`using` 은 RFC-0112 D8 — 부르는 쪽이 위치 인자로
  적지 않고 `let x … using <출처> be …` 로 건넨다. 그 op 안에 출처가 하나뿐이면 적지 않아도 그것이 기본값이다). 여기서 trait 은 "이 op 들을
  갖추었다" 는 인터페이스 계약이고, comptime 타입 인자는 컴파일 시점에 구체 타입으로
  확정된다(단형화). 그래서 vtable 도 간접 호출도 없다 — 갈아끼우기의 실행 비용이 0 이다.
- **OOM 은 값이다**(RFC-0043 D5). OOM(out of memory)이란 남은 자리가 모자란 상황이다.
  `reserve` 는 그때 트랩(프로그램을 멈추는 오류)을 내는 대신 `option` 의 `none` 을
  돌려준다. 값이므로 호출자가 반드시 검사해서 처리한다.
- **돌려주는 것은 오프셋이 아니라 뷰다.** 뷰란 원본 버퍼의 일부를 가리키는
  서브슬라이스다 — 복사본이 아니다. `reserve` 의 결과인 `mut slice u8` 에 쓰면 원본
  버퍼가 바뀐다.
- **안 짓는 것(소스가 정직하게 적어 둔 것)**: 임의 조각의 `free`·리셋·재사용. 이 얼로케이터들은
  범프다. 되돌릴 수 있는 것은 **마지막 조각 하나**뿐이다(`grow` 로 늘리거나 `release` 로 돌려받거나).

- **`grow old newn` — 마지막 조각을 제자리에서 늘린다** (2026-08-18 추가 · 2026-09-13 RFC-0112 D10 에서
  크기 대신 조각을 받게 바뀜). 그 뒤로 아무도 자리를 안 받았으면 늘려도 남의 바이트가 아니다. 판정은
  *"건넨 조각이 마지막으로 준 **바로 그 바이트**인가"* 다 — `same_slice` 로 묻는다. 전에는 길이만 보았고,
  길이만 같은 남의 버퍼가 통과해 두 컨테이너가 조용히 겹쳤다(2026-09-04 보안 검토).
- **`release v` — 마지막 조각을 돌려받는다** (`freeing_allocator` · RFC-0112 D10). 범프는 마지막 조각만
  받는다 — 그 밖의 조각이면 `false` 를 답하고 아무것도 안 한다(받는 척하지 않는다).
  ★ **실패는 값이다**(`none`). 못 늘리면 부르는 쪽이 새로 받아 복사하면 된다 ⇒
    **이것은 최적화이지 계약이 아니고, 없어도 답이 같아야 한다.** 그러니 새 얼로케이터를
    쓸 때 `grow` 를 **`return none .` 한 줄로 두어도 완전한 구현**이다.
    (다만 그 한 줄은 지금 nest 정규화기가 못 감싼다 — 뜻은 같고 나무만 덜 선다. TODO 에 있다.)
  ☞ 값: 성장 벡터(`growvec`)의 아레나 고수위가 요청의 **약 4배 → 2배**가 됐다
    (2,048 B 담는 데 8,104 → 4,088 B). 버려진 세대가 0 이 된다. 스코프 기반 일괄 해제는
  `region` 어휘 블록(SPEC-004 §4.5)의 몫이다. 임의 시점 해제가 필요하면
  [`pool`](pool.md) 을 보라.

## 자료구조

구조체는 없다. 인터페이스 trait 하나와 그것을 충족하는 actor 둘이다. actor 는 자기
상태(state)를 들고 `send` 메시지로만 대화하는 객체다 — 상태에 직접 손을 못 대므로
불변식이 안에서 지켜진다.

```lowent
export trait byte_allocator .
  reserve output option mut slice u8 . . input s self . input n u64 . effects state via self .
  grow    output option mut slice u8 . . input s self . input old mut slice u8 . . input newn u64 . effects state via self .
  used    output u64 . input s self . effects state .
end .

export trait freeing_allocator .
  reserve output option mut slice u8 . . input s self . input n u64 . effects state via self .
  grow    output option mut slice u8 . . input s self . input old mut slice u8 . . input newn u64 . effects state via self .
  used    output u64 . input s self . effects state .
  release output bool . input s self . input v mut slice u8 . . effects state via self .
end .
```

`via self` 는 «이 op 의 효과는 구현이 적은 효과다» 라는 뜻이다(RFC-0112 D7) — 범프의 `reserve` 는
`state` 뿐이고, 힙에서 깎는 `heap_bytes` 의 `reserve` 는 `heap state` 다. 그래서 그 얼로케이터로
단형화한 컨테이너 op 의 서명에 `heap` 이 선다.

두 범프 actor 는 같은 state 를 가진다:

| 필드 | 타입 | 뜻 |
|---|---|---|
| `mem` | `mut slice u8` | 뒷받침 버퍼(호출자가 `init` 으로 건 것) |
| `off` | `u64` | 범프 커서 |
| `last` | `u64` | 마지막 조각의 시작 — `grow`·`release` 가 «마지막인가» 를 묻는 근거 |

불변식: `last <= off <= len mem`.

- `bump_bytes` — 순차 범프. 커서를 요청 크기만큼만 민다. `byte_allocator`·`freeing_allocator` 둘 다.
- `bump_aligned` — 같은 인터페이스, 다른 정책. 할당 시작점을 8 의 배수로 올린 뒤 자른다.
- `fixed_bytes`·`heap_bytes` — 뿌리에서 곧장 깎는 기본 얼로케이터(아래 «기본 얼로케이터»).

## op 한눈에

범프의 op 은 다섯이다: 바닥을 걸고(`init`), 잘라 받고(`reserve`), 마지막 조각을 늘리거나(`grow`)
돌려받고(`release`), 얼마나 썼는지 묻는다(`used`).

| op | 종류 | 시그니처 요약 | 실패 시 |
|---|---|---|---|
| `init` | 범프 핸들러 (trait 밖) | `backing mut slice u8` → `u64` (용량) | 실패 없음 |
| `reserve` | trait | `n u64` → `option mut slice u8` | `none` (커서 안 움직임) |
| `grow` | trait | `old mut slice u8` · `newn u64` → `option mut slice u8` | `none` (마지막 조각이 아니거나 자리 부족) |
| `release` | `freeing_allocator` | `v mut slice u8` → `bool` | `false` (마지막 조각이 아님) |
| `used` | trait | → `u64` (커서 위치) | 실패 없음 |

`init`·`reserve`·`grow`·`release` 는 `effects state .`, `used` 는 `effects none .` 이다(읽기만 한다).
`bump_bytes` 와 `bump_aligned` 둘 다 이 표 그대로다.

## op 상세

**매개변수를 왜 받나 — 이 모듈의 규약.** 이 얼로케이터는 자기 메모리를 갖지 않는다.
누군가 큰 바이트 덩어리(`mem`)를 주면 그것을 앞에서부터 잘라 줄 뿐이다. 그래서 액터를 만든 뒤
`init` 으로 **자를 원본을 먼저 건네야** 한다 — 그것이 이 모듈이 아는 유일한 메모리다.
`reserve n` 의 `n` 은 잘라 줄 크기이고, 남은 자리가 모자라면 `none` 이 온다(트랩이 아니다).
개별 반납이 없는 대신 원본을 통째로 버리면 전부 회수된다 — 그것이 "범프" 의 값이다.


op 하나하나를 시그니처와 함께 본다. 매개변수마다 "왜 이것이 필요한가" 를 한 줄로 붙였다 —
이 라이브러리는 스스로 메모리를 만들지 않으므로 필요한 것을 전부 인자로 받는다.

### init

잘라 쓸 원천 버퍼를 건다. 얼로케이터를 만들면 제일 먼저 한 번 부른다. **trait 에는 없다**
(RFC-0112 · WO-0213) — 뿌리를 딛는 `fixed_bytes`·`heap_bytes` 는 걸 버퍼가 없으므로, «빌린 바이트를
받는다» 는 범프만의 일이다. 그래서 얼로케이터를 받는 제네릭 코드는 `init` 을 부르지 않는다 — 이미
준비된 얼로케이터를 받는다.

```lowent
proc init output u64 . input backing mut slice u8 . . effects state .
```

- `backing` — 잘라 쓸 뒷받침 버퍼. 이 얼로케이터는 스스로 메모리를 만들지 못하므로
  (몰래 할당하지 않는 규율) 바이트는 반드시 밖에서 받아야 한다 — 그 통로가 이 매개변수다.
  누가 준 바이트인지는 묻지 않는다.
- 커서를 0 으로 놓고 `len backing` 을 돌려준다 — 호출자가 용량을 바로 확인할 수 있다.
- 제한 없음. 다시 부르면 새 버퍼로 갈아타고 커서가 0 으로 돌아간다(옛 뷰는 옛 버퍼를 계속 본다).

### reserve

n 바이트를 잘라 받는다. 이 모듈의 중심 op 이다.

```lowent
proc reserve output option mut slice u8 . . input n u64 . effects state .
```

- `n` — 원하는 바이트 수. 커서를 얼마나 밀지, 뷰가 얼마나 길지를 이 값이 정한다.
- 성공: `some <뷰>` — 뒷받침 버퍼 안 `n` 바이트의 `mut slice u8`. 커서가 `n` 만큼 전진한다.
- 실패: 남은 자리가 모자라면 `none`. **커서는 안 움직인다** — 부분 할당은 없다.
- `bump_aligned` 판은 시작점을 먼저 8 의 배수로 올린다
  (`pad = mod (sub 8 (mod off 8)) 8`). 정렬이 필요한 자료(u64 배열 등)를 담을 때 쓴다.
  패딩 바이트는 버려진다 — 범프라 되돌릴 수 없다.

### grow

마지막 조각을 제자리에서 늘린다. 실패는 값이다 — 못 늘리면 부르는 쪽이 새로 받아 복사한다.

```lowent
proc grow output option mut slice u8 . . input old mut slice u8 . . input newn u64 . effects state .
```

- `old` — 늘릴 조각. **마지막으로 준 바로 그 바이트**여야 한다(`same_slice`). 길이만 같은 남의
  조각은 `none` 이다.
- `newn` — 새 길이(`len old` 이상). 남은 자리가 모자라면 `none`.
- 성공: `some <늘어난 뷰>` — 앞부분은 옛 조각과 같은 바이트다(복사가 없다).
- 이것은 최적화이지 계약이 아니다 — 새 얼로케이터는 `grow` 를 `return none .` 한 줄로 둬도 완전하다.

### release

마지막 조각을 돌려받는다(`freeing_allocator`).

```lowent
proc release output bool . input v mut slice u8 . . effects state .
```

- `v` — 돌려줄 조각. 마지막으로 준 바로 그 바이트면 커서를 그 시작으로 되돌리고 `true`.
- 그 밖의 조각이면 `false` — 아무것도 안 바뀐다. 모르는 조각을 조용히 받아 두면 두 번 돌려주기가
  남의 자리를 지우기 때문이다.

### used

지금까지 얼마나 썼는지 묻는다. 남은 자리 계산이나 통계에 쓴다.

```lowent
proc used output u64 . effects none .
```

- 매개변수가 없다 — 얼로케이터 자신의 커서를 읽을 뿐이다.
- 지금까지 소비한 바이트 수(= 커서 위치)를 돌려준다. `bump_aligned` 에서는 패딩도 포함이다.

## 사용법과 예제

기본 흐름은 "actor 를 만들고 → `init` 으로 버퍼를 걸고 → `reserve` 로 잘라 쓴다" 이다.

std 에 설치돼 있으므로 from 없이 쓴다. 별칭도 한 줄이다:

```lowent
use allocs .            rem from-생략 — 예약(std) 이름으로 해소된다
use allocs as al .      rem 별칭 관례
```

기본 사용(`impl/tests/vm_allocbytes.low` 의 `borrowed` 를 줄인 것):

```lowent
proc borrowed output u64 . input buf mut slice u8 . . effects state . do
  var a allocs.bump_bytes be spawn actor allocs.bump_bytes . .   rem 얼로케이터 actor 를 만든다
  let cap0 u64 be send a init buf .                 rem 뒷받침 버퍼를 건다 — cap0 = len buf
  let p option mut slice u8 . . be send a reserve 3 .   rem 3 바이트를 잘라 달라고 한다
  guard is_some p . else return 91 .                rem OOM 검사 — none 이면 여기서 빠진다
  let pv mut slice u8 . be some_value p .           rem 검사를 통과했으니 값을 꺼낸다
  set (index pv 0) 65 .        rem ★ 뷰다 — buf[0] 이 65 로 바뀐다 (복사본이 아니다)
  return send a used .         rem 지금까지 소비한 바이트 수 = 3
end
```

구현을 매개변수로 받는 코드 — 어느 얼로케이터인지 모른 채 돈다:

```lowent
proc two_from .
  output u64 .
  input comptime a type .                rem 얼로케이터 "타입" — 컴파일 시점에 확정된다
  using al a .                           rem 그 타입의 actor 인스턴스 — 부르는 쪽이 using 으로 건넨다
  effects state via a .                  rem 비용은 그 얼로케이터의 reserve 가 내는 효과다
  requires allocs.byte_allocator a .     rem a 는 이 trait 을 충족해야 한다
do
  let p option mut slice u8 . . be send al reserve 3 .   rem 어느 구현이든 같은 op
  guard is_some p . else return 91 .
  let q option mut slice u8 . . be send al reserve 5 .
  guard is_some q . else return 92 .
  let g option mut slice u8 . . be send al grow (some_value q) 9 .   rem 마지막 조각을 9 로
  return send al used .
end .

proc borrowed2 output u64 . input buf mut slice u8 . . effects state . do
  var b allocs.bump_bytes be spawn actor allocs.bump_bytes . .
  let c u64 be send b init buf .         rem init 은 범프에게 직접 — trait 에는 없다
  let n u64 using b be two_from .        rem 얼로케이터는 인자 자리가 아니라 using 으로
  return n .                             rem 3 + 5 를 9 로 늘렸으니 12
end .
```

같은 코드에 `allocs.bump_aligned` 를 주면 정렬 때문에 둘째 조각이 8 에서 시작해 17 이 된다.
뿌리에서 곧장 깎는 `allocs.heap_bytes` 를 주면 `grow` 는 `none`(뿌리는 마지막이 누구 것인지 모른다)이고,
`two_from` 인스턴스의 서명에 `heap` 이 선다.

## 반례 — 이렇게 쓰면 안 된다

실제로 자주 밟는 잘못된 코드들이다. 각 반례에 **증상**을 적었다 — 컴파일 때 잡히는지,
실행 중에 멈추는지, 아니면 조용히 틀린 값이 되는지가 대처를 가른다.

```lowent
rem ✗ none 검사 없이 값을 꺼낸다
let p option mut slice u8 . . be send a reserve 99 .
let pv mut slice u8 . be some_value p .    rem 버퍼가 4 바이트면 → E-VM-NONE
```

증상: 실행 중 **E-VM-NONE 트랩(패닉)** 으로 멈춘다 — `some_value of none`. 컴파일은
통과하므로 방심하기 쉽다. OOM 은 값이니 `guard is_some p . else …` 가 먼저다.

```lowent
rem ✗ 권한 없이 뿌리에 닿으려 한다
proc f output u64 . effects state . do
  let m option mut slice u8 . be alloc_bytes ??? capacity 64 .   rem cap allocator 도 cap heap 도 없다
```

증상: **컴파일 단계에서 거절** — 실행까지 가지도 못한다. 뿌리 권한(`cap allocator` 또는 `cap heap`)을
인자로 받지 않은 op 은 `alloc_bytes` 를 부를 수 없다(E-ALLOC-NOCAP · 힙이면 E-HEAP-NOCAP). `allocs` 는 그
대체가 아니다 — 뒷받침 바이트는 결국 누군가에게서 받아야 한다. 권한 칸을 든 `heap_bytes` 를 권한 없이
`spawn` 해도 거절된다(E-CAP-FORGE).

```lowent
rem ✗ fn 에서 send 를 부른다
fn g output u64 . input a allocs.bump_bytes . do
  return send a used .        rem 핸들러는 effects state 다
```

증상: **컴파일 에러 E-EFFECT-CALC** — `effects none` 인 fn 은 상태를 만지는 핸들러를
부를 수 없다. 이 코드는 proc 이어야 한다.

```lowent
rem ✗ trait 을 충족하지 않는 값을 출처로 건넨다 (c 는 u64)
let x u64 using c be two_from .
```

증상: **컴파일 에러 E-BOUND-UNSAT** — `u64` 는 `byte_allocator` 를 충족하지 않는다.
`reserve`/`grow`/`used` 핸들러를 가진 actor 타입만 들어간다.

## 주의사항

범프의 본성에서 오는 함정들이다 — 전부 "해제가 없다" 에서 나온다.

- **마지막 조각 말고는 해제가 없다.** 오래 사는 얼로케이터에 반복 할당하면 언젠가 `none` 만 나온다.
  받고-놓기를 반복하는 모양이면 [`pool`](pool.md) 이 맞다.
- `reserve` 가 낸 뷰는 뒷받침 버퍼의 **별칭**이다. `init` 을 다시 불러도 옛 뷰는 사라지지
  않는다 — 겹쳐 쓰는 것은 호출자 책임이다.
- `bump_aligned` 의 패딩은 `used` 에 포함되고 회수되지 않는다.
- actor 는 순차 배달 전제다. 스레드 안전 장치는 없다 — 그래서 범프를 태스크에 건네면 거절된다
  (E-ALLOC-SHARED: 태스크에 건네는 얼로케이터는 `reserve` 가 `atomic` 이어야 한다). 태스크로 띄우는 op 이
  뿌리에서 곧장 깎는 것도 거절이다(E-ALLOC-TASK).

## 기본 얼로케이터 — `fixed_bytes` · `heap_bytes` (RFC-0112 D6)

뿌리에서 곧장 깎는 얼로케이터도 **평범한 actor** 이고 `byte_allocator` 를 갖춘다. 그래서 컨테이너는
«기본 얼로케이터냐 사용자 얼로케이터냐» 를 묻지 않는다 — 같은 `using` 자리에 들어간다. 다른 점은 받침이
버퍼가 아니라 **권한 칸**이라는 것 하나다.

| actor | state | `reserve` 의 효과 | 쓸 수 있는 곳 |
|---|---|---|---|
| `fixed_bytes` | `root cap allocator .` · `got u64 .` | `alloc state` | 어디서나(베어메탈은 링커 창) |
| `heap_bytes` | `root cap heap .` · `got u64 .` | `heap state` | 호스티드만(베어메탈은 E-HEAP-NOHOST) |

- 권한 칸은 실행 중 값이 아니다 — 그 actor 를 `spawn` 하는 op 이 **같은 종류의 권한을 쥐고 있어야** 한다
  (E-CAP-FORGE). 권한 없는 곳에서 한 줄로 힙을 지어낼 수 없다.
- 둘 다 `init` 이 없고, `grow` 는 언제나 `none` 이다(뿌리는 마지막 할당이 누구 것인지 모른다).
- 쓰는 꼴(`impl/tests/vm_capactor.low` 를 줄인 것):

```lowent
export proc main output u8 . input h cap heap . input al cap allocator . effects heap alloc state .
do
  var hb allocs.heap_bytes be spawn actor allocs.heap_bytes . .     rem cap heap 을 쥐었으니 띄울 수 있다
  let vo option (vecgen.vec u32 allocs.heap_bytes) . using hb be vecgen.open u32 16 .
  guard is_some vo . else return 1 .
  return 0 .
end .
```
- 오라클: 바닥이 0 에서 시작하는 범프라 VM 과 네이티브가 같은 바이트를 본다. 주소를
  노출하지 않으므로 diff-sweep 이 덮는다(`vm_allocbytes.low` 가 그 증인이다).
