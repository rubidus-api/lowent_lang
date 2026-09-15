# pool — 세대 핸들 블록 풀

소스: `lib/pool.low` · 모듈명 `pool` (사용자 설계, 2026-07-19)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 고정 크기 블록을 빌려 주고 돌려받는 **풀**이다. 핸들에 세대 번호가 붙어 옛 핸들을 알아본다.

**언제 쓰나.** 객체를 만들고 지우기를 반복할 때, 이미 지운 것을 실수로 다시 쓰는 버그를 막고 싶을 때 쓴다.

**최소 예제.** 아래가 이 모듈을 쓰는 가장 짧은 꼴이다. 각 줄이 무엇을 하는지는 주석에 적었다.

```lowent
use pool .

newtype pa u8 .                                    rem 이 풀의 **브랜드** — 선언 하나가 곧 저장소 하나다

let po option (pool.block_pool pa) . be pool.init pa mem gens 4096 .
guard is_some po . else return 1 .
var p pool.block_pool pa . be some_value po .      rem 풀이 mem·gens 를 봉해 든다
let h option (pool.handle pa) . be pool.take pa p .   rem 블록 하나를 빌린다
rem 돌려준 뒤 옛 핸들로 접근하면 세대가 달라 거절된다 — 그것이 이 모듈의 요점이다
```

> ### ⚠ 무엇을 막아 주고, 무엇을 안 막아 주나
>
> **막아 준다 ①** — 같은 풀에서 빌린 블록을 돌려준 뒤 **옛 핸들로 접근**하는 것. 세대가
> 달라 거절된다(런타임). 이 모듈의 요점이 그것이다.
>
> **막아 준다 ② (2026-08-28 봉인, RFC-0104 §8-2)** — **풀을 섞는 것.** 핸들과 풀이 **브랜드**를
> 타입으로 들기 때문에 `handle pa` 를 `pool pb` 에 넣으면 **컴파일 에러 `E-TYPE-INSTANCE`** 다.
> 그리고 `mem`·`gens` 는 `init` 이 봉해 들어 **op 이 더 이상 받지 않는다** — 엉뚱한 배열을
> 건네는 길이 표면에서 사라졌다. 브랜드는 값이 아니라 **타입**이라 핸들 칸은 안 는다(비용 0).
>
> **안 막아 준다** — 브랜드는 **선언마다** 하나다. 브랜드를 comptime 인자로 받아 `init` 하는
> op 을 두 번 부르면 한 브랜드가 풀 둘을 덮는다. **풀 하나에 브랜드 하나**를 지키는 것은
> 당신의 몫이다(자리-생성형 brand 는 RFC-0104 §5.11 의 후속).
>
> ★ 그리고 이것은 *"언어에 내장된 세대 핸들"* 이 **아니다**. `pool` 은 평범한 라이브러리이고
> 빌트인 op 증가가 0 이다 — 안전은 이 모듈의 규율에서 나오지 언어가 강제하지 않는다.
>   (브랜드도 새 낱말이 아니다: `newtype` 과 `input comptime b type .` 은 이미 있던 것이다.)

**읽는 순서.****읽는 순서.** 급하면 «op 한눈에» 표만 봐도 된다. 왜 이렇게 생겼는지가 궁금하면 «왜 있는가»부터, 실제로 쓰다 막히면 «반례»와 «주의사항»을 먼저 보면 된다.

## 왜 있는가

이 모듈은 **같은 크기의 메모리 블록을 받았다 놓았다를 반복하고 싶을 때** 쓴다.
[`allocs`](allocs.md) 의 범프는 해제가 없어서 이런 모양에 못 쓰고, 이 풀은 해제한 블록이
되돌아와 다시 쓰인다.

풀려는 문제는 이것이다: *"필요할 때마다 4k 씩 뭉텅이로 받아 여러 용도로 쓰고, 부족하면 더
받고, 원하는 시점에 특정 블록을 새 블록에 복사하고 기존 것은 해제하고."* 해제 시점이
**어휘적이지 않으므로**(코드 블록의 끝과 무관하므로) `region` 블록(스코프가 곧 수명)으로는
안 되고, 범프(해제 없음)로도 안 된다.

그래서 정적 검사 대신 **세대(generation) 핸들**로 간다. 원리는 단순하다: 블록마다 "세대"
번호를 두고, 블록을 받을 때 핸들에도 그 번호를 적어 준다. 해제하면 블록의 세대가 올라가고,
그 순간 옛 핸들은 번호가 안 맞아 **자동으로 무효**가 된다 — 해제한 메모리를 다시 쓰는 버그
(use-after-free)가 값 검사 하나로 걸린다. 이것은 이 언어가 이미 고른 답이다(SPEC-004
§108: use-after-free 는 세대 핸들이 처리한다). 언어가 이것을 빌트인으로 넣지 않은 이유:
자유 목록도 세대 배열도 검사도 전부 Lowent 로 써지므로 라이브러리다(리프 규칙).
**빌트인 op 증가 0.**

## 설계 의도와 경계

핵심은 "고정 크기 + 세대 검사" 두 가지다. 나머지 설계는 전부 거기서 따라 나온다.

- **고정 크기 블록**이라 해제가 쉽다: 자유 목록(해제된 블록을 이어 둔 목록)에 넣는 것이
  전부고 단편화가 0 이다. 크기별 관리·합치기 같은 범용 힙의 어려움이 통째로 없다.
- **배치는 SoA 다.** SoA(Structure of Arrays)란 한 덩어리 구조체 배열 대신 **필드마다
  배열 하나**를 나란히 두는 배치다([`soa`](soa.md) 참조). 평범한 `slice` 는 손대지
  않는다(여전히 `{ptr, len}`, 비용 0). 세대는 나란한 배열(`gens`)에 살고, 별도 타입
  `handle` 만 세대를 하나 든다. 검사 비용은 `bytes` 라는 **문을 지나는 코드에만** 붙는다.
- **두 배열(`mem`·`gens`)은 호출자가 마련하고, 풀이 봉해 든다.** 라이브러리는 할당하지 않고
  버퍼를 받는다 — 이 저장소의 확립된 규율이다. 바뀐 것은 *누가 들고 다니느냐* 다(2026-08-28
  봉인): `init` 에 한 번 주면 그 뒤 op 은 안 받는다. 그래서 풀은 **actor 가 아니라 struct** 다
  — actor state 는 슬라이스를 못 든다.
- **자유 목록 링크는 블록 자기 바이트 안에 산다(intrusive).** intrusive 란 장부를 별도
  메모리가 아니라 대상 자신 안에 두는 방식이다. 해제된 블록은 세대가 올라 아무도 못
  닿으므로 그 자리를 장부로 쓰는 것이 공짜다. 추가 배열 요구 0.
- **안 짓는 것(정직히)**: 블록 경계를 넘는 할당(한 핸들 = 한 블록 안) · 자동 압축 ·
  스레드 안전(순차 배달 전제) · 세대 넘침 뒤 재사용(u64 라 현실적으로 안 돈다).
- 남는 창 하나 — `bytes` 로 빌린 슬라이스를 **든 채** `release` 하면 그 슬라이스는 여전히
  쓸 수 있다 — 는 언어의 `borrow <이름> be <식> do … end` 블록이 어휘적으로 닫는다
  (아래 사용법 참조).

## 자료구조

값 타입 둘이다: `handle b` 와 `block_pool b`. **둘 다 브랜드 `b` 를 타입 파라미터로 받는다** —
`lib/mapgen.low` 의 `table k v` 와 같은 모양이다.

```lowent
export struct handle do
  input comptime b type .
  blk u64 .
  len u64 .
  gen u64 .
end .
```

핸들이 평범한 slice 와 다른 점의 전부는 **세대를 값으로, 브랜드를 타입으로** 들고 다닌다는
것이다. 브랜드는 칸을 차지하지 않는다.

`block_pool b` 의 필드:

| 필드 | 타입 | 뜻 |
|---|---|---|
| `mem` | `mut slice u8` | 블록들이 사는 바이트 (**봉인** — `init` 이 받아 든다) |
| `gens` | `mut slice u64` | 블록마다의 세대 (**봉인**) |
| `bsize` | `u64` | 블록 크기 (`init` 의 `bs`) |
| `nblk` | `u64` | 블록 수 (두 배열 중 작은 쪽이 정한다) |
| `freetop` | `u64` | 순차 배당 커서(꺼내 본 적 없는 첫 블록) |
| `freehead` | `u64` | 자유 목록 머리. **인덱스 + 1** — 0 이 "비었음" |

불변식:
- `gens[b]` 가 블록 `b` 의 권위 있는 현재 세대다. 0 = 한 번도 안 준 블록, `init` 이 1 로
  놓는다. `release` 가 올린다.
- 핸들이 사는 조건: `h.gen == gens[h.blk]`. 어긋난 핸들로는 어떤 문도 안 열린다.
- **브랜드가 같아야 문이 열린다**: `handle pa` 는 `block_pool pa` 에만 들어간다(컴파일타임).
- 해제된 블록의 **앞 8 바이트**는 자유 목록 링크(리틀엔디언 u64, "다음 인덱스 + 1")다.

## op 한눈에

수명 순서대로 읽으면 된다: `init`(준비) → `take`(받기) → `bytes`(쓰기) → `release`(놓기),
그리고 관측용 넷(`blocks`·`alive`·`used`·`outstanding`)이다.

전부 모듈 수준 op 이고 **첫 인자가 브랜드**다. 바꾸는 op 은 풀을 `mut` 으로 받는다.
효과는 `none` — 바뀌는 것은 호출자가 준 `mut` 값이고 그것은 **타입이 이미 말한다**.

| op | 시그니처 요약 | 실패 시 |
|---|---|---|
| `init` | `comptime b`, `mem mut slice u8`, `g mut slice u64`, `bs u64` → `option (block_pool b)` | `bs < 8` 이면 `none` |
| `blocks` | `comptime b`, `p block_pool b` → `u64` | 실패 없음 |
| `take` | `comptime b`, `p mut block_pool b` → `option (handle b)` | 블록 없으면 `none` |
| `release` | `comptime b`, `p mut block_pool b`, `h handle b` → `bool` | 낡은/범위 밖 핸들이면 `false` |
| `bytes` | `comptime b`, `p block_pool b`, `h handle b` → `option mut slice u8` | 낡은/범위 밖 핸들이면 `none` |
| `alive` | `comptime b`, `p block_pool b`, `h handle b` → `bool` | 실패 없음(거짓이 답) |
| `used` | `comptime b`, `p block_pool b` → `u64` | 실패 없음 |
| `outstanding` | `comptime b`, `p block_pool b` → `u64` | 실패 없음 |

## op 상세

**왜 브랜드를 매번 적나.** 이 언어에는 **추론되는 타입 파라미터가 없다**(SPEC-004 §152:
인스턴스화가 소스에 보인다). 그래서 `pool.take pa p` 처럼 브랜드를 적는다 — 그 한 낱말이
*"이 핸들은 저 풀의 것"* 이라는 계약이고, 값 한 칸도 안 쓴다.

### init

풀을 차리고 **봉한다**. 블록들이 살 바이트와 세대 배열을 걸고 블록 크기를 정한다.

```lowent
proc init output option (block_pool b) . input comptime b type . input mem mut slice u8 . .
  input g mut slice u64 . . input bs u64 . effects none .
```

- `b` — 이 저장소의 **브랜드**. `newtype pa u8 .` 처럼 선언한 이름을 넘긴다.
- `mem` — 블록들이 살 바이트. 풀은 스스로 할당하지 않으므로 밖에서 받는다.
- `g` — 블록마다 세대 하나를 담을 배열. 세대 검사의 근거 장부가 이것이다.
- `bs` — 블록 크기(예: 4096). 한 핸들이 받는 바이트 수가 이 값으로 고정된다.
- 블록 수 `n = min(len mem / bs, len g)` 를 정하고 세대를 전부 1 로 놓는다. 그 수는 `blocks` 로 묻는다.
- **`bs >= 8` 이어야 한다** — 자유 목록 링크 한 칸이 u64 다. 작으면 `none` 이다(트랩 아님).

### blocks

이 풀이 든 블록 수. 전에 `init` 이 돌려주던 그 수다.

```lowent
fn blocks output u64 . input comptime b type . input p block_pool b .
```

### take

블록 하나를 받는다. 핸들이 그 블록의 "열쇠" 다.

```lowent
proc take output option (handle b) . input comptime b type . input p mut block_pool b .
  effects none .
```

- **놓은 것을 먼저 준다**: 자유 목록이 비어 있을 때만 순차 배당으로 간다. 그래서
  받고-놓기를 반복해도 풀이 마르지 않는다. 자유 목록은 LIFO 다(마지막에 놓은 것이 먼저).
- 성공: `some (handle b)` (`len` = 블록 크기, `gen` = 그 블록의 현재 세대).
- 남은 블록이 없으면 `none` — 트랩이 아니다(OOM 은 값이다, RFC-0043 D5).
- 재사용된 블록의 핸들은 세대가 올라간 **새 핸들**이다. 옛 핸들은 되살아나지 않는다.

### release

블록을 되돌려 놓는다. 이 순간부터 옛 핸들은 전부 무효다.

```lowent
proc release output bool . input comptime b type . input p mut block_pool b .
  input h handle b . effects none .
```

- 세대를 올린다 — 그 순간 그 블록의 **모든 옛 핸들이 낡는다.** 그 뒤 블록을 자유 목록에
  되돌린다(링크는 블록 자기 앞 8 바이트에 쓴다).
- `h.blk` 가 범위 밖이거나 세대가 안 맞으면(이미 낡음 = 이중 해제 포함) `false`.
- 다른 브랜드의 핸들은 **여기까지 오지도 않는다** — 컴파일타임에 거절된다.

### bytes

핸들을 실제 바이트로 바꾼다 — **바이트에 닿는 유일한 문**이다.

```lowent
proc bytes output option mut slice u8 . . input comptime b type . input p block_pool b .
  input h handle b . effects none .
```

- 세대가 맞으면 그 블록의 `mut slice u8` 서브슬라이스를, 안 맞으면 `none` —
  use-after-free 가 여기서 걸린다. 검사 비용은 이 문을 지나는 코드에만 붙는다.
- **어느 backing 인지 묻지 않는다** — 풀이 자기 것을 안다(봉인).

### alive

핸들이 아직 유효한지 묻기만 한다.

```lowent
proc alive output bool . input comptime b type . input p block_pool b .
  input h handle b . effects none .
```

- 바이트는 안 꺼낸다. 디버깅·단언(assert)에 쓴다.

### used / outstanding

풀의 소비 상태를 관측한다. 둘의 뜻이 다르니 주의한다.

```lowent
fn used output u64 . input comptime b type . input p block_pool b .
proc outstanding output u64 . input comptime b type . input p block_pool b . effects none .
```

- `used` — 순차로 **꺼내 본** 블록 수(`freetop`). 자유 목록에 돌아온 것은 반영 안 된다.
- `outstanding` — 지금 **밖에 나가 있는** 블록 수. 자유 목록을 걸어서 길이를 세어 뺀다.
  누수 감시("놓는 것을 빼먹지 않았나")에 쓴다.

## 사용법과 예제

기본 흐름: 받고(`take`) → 문을 열고(`bytes`) → 빌림 안에서 만지고(`borrow`) → 빌림
밖에서 놓는다(`release`).

```lowent
use pool .              rem from-생략 std 해소
use pool as p .         rem 별칭 관례
```

`impl/tests/vm_pool.low` 의 `borrow_then_release` 를 줄인 것:

```lowent
newtype demo_brand u8 .          rem 이 풀의 브랜드 — 선언 하나가 곧 저장소 하나다

proc demo output u64 . input mem mut slice u8 . . input gens mut slice u64 . .
  effects none .
do
  let po option (pool.block_pool demo_brand) . be pool.init demo_brand mem gens 16 .
  guard is_some po . else return 89 .               rem bs < 8 이면 none
  var p pool.block_pool demo_brand . be some_value po .   rem 풀이 mem·gens 를 봉해 든다
  let h option (pool.handle demo_brand) . be pool.take demo_brand p .   rem 블록 하나를 받는다
  guard is_some h . else return 91 .                rem 풀이 말랐으면 none
  let hh pool.handle demo_brand . be some_value h . rem 핸들을 꺼낸다

  let b option mut slice u8 . . be pool.bytes demo_brand p hh .   rem 문을 연다
  guard is_some b . else return 92 .                rem 낡은 핸들이면 none
  let bv mut slice u8 . be some_value b .           rem 이제 평범한 슬라이스다

  var total u64 be 0 .
  borrow v be bv do                rem 빌림 블록 — 이 안에서만 바이트를 만진다
    set (index v 8) 3 .            rem 앞 8 바이트(링크 자리)를 피해 자료를 넣는다
    set (index v 9) 4 .
    set total (add (narrow u64 (index v 8)) (narrow u64 (index v 9))) .
  end                              rem 빌림이 여기서 끝난다 — v 는 더 못 쓴다

  let rel bool be pool.release demo_brand p hh .  rem 해제는 빌림 밖에서
  guard eq rel true . else return 94 .            rem false 면 낡은 핸들이었다는 뜻
  let dead option mut slice u8 . . be pool.bytes demo_brand p hh .
  guard eq (is_some dead) false . else return 95 .   rem 옛 핸들은 낡았다 — none 이 정상
  return total .                                     rem 7
end
```

## 반례 — 이렇게 쓰면 안 된다

여기 모은 잘못된 코드에는 각각 **증상**을 적었다. 이 모듈의 실패는 트랩보다 `false`·`none`
쪽이 많다 — 반환값을 안 보면 아무 일도 안 일어난 것처럼 보인다는 뜻이다.

```lowent
rem ✗ 해제한 핸들로 다시 닿으려 한다
let freed bool be pool.release pa p hh .
let b2 option mut slice u8 . . be pool.bytes pa p hh .
let bv mut slice u8 . be some_value b2 .
```

증상: `bytes` 가 `none` 을 내고(트랩 아님), 검사 없이 `some_value` 를 부른 그 줄에서
**E-VM-NONE 트랩(패닉)** 으로 멈춘다. `release` 가 세대를 올렸으므로 `bytes` 는 `none`,
`alive` 는 `false` 다.

```lowent
rem ✗ 이중 해제
let once bool be pool.release pa p hh .     rem true
let twice bool be pool.release pa p hh .    rem false — 이미 낡은 핸들
```

증상: 트랩도 에러도 없다 — 두 번째 `release` 가 조용히 `false` 를 돌려줄 뿐이다.
반환값을 안 보면 "놓았다고 믿었는데 안 놓인" 논리 버그가 숨는다. `guard eq rel true .`
로 받는 습관이 답이다.

```lowent
rem ✗ 빌린 이름을 블록 밖으로 내보낸다
borrow v be bv do
  set out v .
end
```

증상: **컴파일 에러 E-BORROW-ESCAPE** — 빌림은 블록 끝에서 끝나므로 밖으로 내보낼 수
없다. 실행까지 가지 않는다.

```lowent
rem ✗ 블록 앞 8 바이트에 살아남아야 할 값을 두고 해제한다
set (index bv 0) 77 .                       rem 앞 8 바이트에 썼다
let r bool be pool.release pa p hh .       rem 해제가 이 자리를 자유 목록 링크로 덮는다
```

증상: 에러는 전혀 없지만, 해제 뒤 그 블록의 **앞 8 바이트 값이 링크로 바뀌어 있다.**
어차피 세대가 올라 아무도 그 블록에 못 닿으므로 안전하지만, "해제 뒤에도 메모리에 남아
있겠지" 라는 기대는 앞 8 바이트에서는 틀린다(`vm_pool.low` 의 `stale_after_release` ④ 참조).

```lowent
rem ✗ 풀 A 의 핸들을 풀 B 에 묻는다
let ha option (pool.handle pa) . be pool.take pa a .
let q bool be pool.alive pb b (some_value ha) .
```

증상: **컴파일 에러 `E-TYPE-INSTANCE`** — `handle pa` 와 `handle pb` 는 다른 구체 타입이다.
실행까지 가지 않는다(음성 픽스처: `impl/tests/vm_poolmix.low`). 봉인 전에는 이 프로그램이
`check: ok` 였고 답은 **뜻이 없었다**(다른 풀의 세대를 보고 판정했다).

```lowent
rem ✗ 블록 크기 8 미만
let po option (pool.block_pool pa) . be pool.init pa mem gens 4 .
```

증상: `init` 이 `none` 을 돌려주며 거절한다(트랩 아님). 그것을 안 보고 `some_value` 를 부르면
**그 줄에서 E-VM-NONE 트랩**이다 — 차린 직후 `guard is_some po .` 이 싸다.

## 주의사항

대부분 "배열은 호출자 것" 이라는 설계에서 오는 규율이다.

- **`mem`·`gens` 는 `init` 에 한 번만 준다**(2026-08-28 봉인). 그 뒤 op 은 안 받으므로
  "다른 배열을 실수로 건네는" 부류의 버그가 **표면에서 사라졌다**.
- **풀 하나에 브랜드 하나.** 브랜드는 선언마다 하나이므로, 브랜드를 받아 `init` 하는 op 을
  두 번 부르면 한 브랜드가 풀 둘을 덮는다. 그것만은 아직 당신의 몫이다.
- 핸들은 값이라 복사해 들고 다닐 수 있지만, 어느 복사본으로든 `release` 하면 전부 낡는다.
- `used` 와 `outstanding` 은 다르다: 받고-놓기를 아무리 반복해도 `outstanding` 이 늘지
  않는 것이 자유 목록이 사는 증거다(`used` 는 처음 몇 라운드만 는다).
- `outstanding` 은 자유 목록을 걷는다 — O(자유 블록 수)다. 뜨거운 경로에서 매번 부르지 않는다.
- 한 핸들 = 한 블록이다. 블록보다 큰 것이 필요하면 블록 크기를 키워서 `init` 한다.
- 순차 배달 전제다. 여러 곳에서 동시에 보내는 설계는 지원하지 않는다.
- 이름 하나의 교훈: op 이름을 `live` 같은 흔한 낱말로 짓지 않는다 — 평평한 형태의
  정규화기가 지역 변수와 op 머리를 못 가른다(그래서 `outstanding` 이다).
