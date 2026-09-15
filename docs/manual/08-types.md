# 8. 나만의 타입 — `struct` · `enum` · `trait`

[← 목차](README.md) · [← 7. guard 와 option](07-guard-and-option.md)

## `struct` — 필드의 묶음

**`struct`** = 이름 붙은 필드의 묶음. `make` 로 만들고 `field` 로 읽는다(붙임 점 `p.x` 는 없다 — `E-FIELD-GLUED`).

```lowent
rem ✓ 점(point) 하나 — 필드는 무두 블록(do 없이 end 로 닫음)에 적는다.
struct point do
  x u64 .
  y u64 .
end
fn origin output point . do
  return make point do x 3 . y 4 . end
end
fn getx input p point . output u64 . do
  return field p x .          rem 다단도 된다: `field o inner deep`
end
```

## `enum` — 여러 경우 중 하나 (페이로드 가능)

**`enum`** = 여러 경우 중 하나. 각 경우(변형)는 **페이로드**(딸린 값)를 가질 수 있다.
만들 때는 `<enum>.<변형>` 으로, 가를 때는 `isa`(태그 확인)·`get`(페이로드 꺼내기)로:

갈래마다 **점으로 닫는다**(`red .`). 개행은 닫개가 아니므로, 점 없이 줄마다 적으면 여러 갈래가
**한 갈래로 이어진다**.

```lowent
rem ✓ 페이로드 enum — lit 은 수 하나, add 는 자식 둘(아레나 인덱스).
enum node do
  lit v u32 .
  add l u32 r u32 .
end
fn leaf output node . do return node.lit 42 . end
fn is_leaf input n node . output u8 . do
  guard isa n lit . else return 0 .     rem lit 변형인지 확인
  return 1 .
end
```

> `enum` 변형이 **자기 타입을 값으로** 담으면(`add l node .`) 크기가 무한이라 거절된다
> (`E-ENUM-INFINITE`) — 재귀는 위처럼 **인덱스**(`u32`)나 `owned` 로 끊는다. 그래서 AST 같은
> 나무 구조는 노드를 `slice node` 아레나에 담고 인덱스로 잇는다.

## `range` — 타입이 좁아지면 검사가 사라진다

정수 파라미터가 실제로는 좁은 범위만 받는다면, **타입 자리에서** 그렇게 적을 수 있다.

```lowent
fn scale input a range 0 100 . output u8 . do
  return narrow u8 (mul a 2) .          rem 200 을 못 넘는다 — 검사가 필요 없다
end
```

`range 0 100` 은 *"0 이상 100 이하"* 다. 이것은 주석이 아니라 **타입**이다:

- 호출자가 범위를 못 지키면 **진입에서** 걸린다,
- 본문의 산술은 그 범위를 **사실로** 쓴다 ⇒ 넘침 검사가 **지워진다**.

`requires le a 100 .` 로도 같은 일을 할 수 있다. 다른 점은 **어디에 적히는가**다 —
`range` 는 **시그니처에** 적히므로 부르는 쪽이 계약을 **보고** 쓴다.

☞ 별칭에도 실려 넘어간다: `type pct range 0 100 .` 을 만들면 `pct` 를 받는 모든 자리가
그 범위를 물려받는다.

## `trait` — 타입이 갖출 op 의 목록

트레이트는 *"이 타입은 이런 op 들을 갖고 있다"* 를 **검사받는 약속**으로 적는다. op 을 여럿 적을 수 있다.

**왜 쓰나.** 사각형과 정사각형은 넓이를 구하는 방법이 다르지만, *"넓이를 알려 준다"* 는 약속은 같다. 그 약속에
이름(`shape`)을 붙이면, *"넓이를 알려 주는 것이면 무엇이든"* 받는 op 을 **한 번만** 쓸 수 있다.

```lowent
module ex_trait_why .

rem 모양마다 넓이를 구하는 법은 다르다. 그러나 «넓이를 알려 준다» 는 약속은 같다.
trait shape do
  area input s self . output u64 .
end .

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end .

struct square do
  satisfies shape .
  side u64 .
end .

fn rect.area input s rect . output u64 .
do
  return mul (field s w) (field s h) .
end .

fn square.area input s square . output u64 .
do
  return mul (field s side) (field s side) .
end .

rem 이 op 은 **어떤 모양이든** 받는다 — `requires shape t` 가 «넓이를 알려 주는 타입만» 이라고 못박는다.
fn double_area input comptime t type . input s t . output u64 .
  requires shape t .
do
  return mul 2 (method s area) .
end .

fn demo output u64 .
do
  let r rect be make rect do w 2 . h 3 . end .
  let q square be make square do side 4 . end .
  return add (double_area rect r) (double_area square q) .
end .
```

- `double_area` 는 `rect` 도 `square` 도 받는다. `requires shape t` 가 *"`shape` 를 갖춘 타입만"* 이라고 못박는다.
- `method s area` 는 `s` 의 타입에 붙은 `area` 를 부른다 — `rect` 면 `rect.area`, `square` 면 `square.area`.
- 약속을 안 지킨 타입(예: `area` 를 안 만든 타입)은 번역 때 거절된다. 이 언어에는 상속이 없으니, 여러 타입을
  한 이름으로 다루는 길은 이것이다.

아래는 op 을 여럿 가진 트레이트다.

```lowent
module shapes .

trait shape do
  area input s self . output u64 .
  grow input s self . input k u64 . output self .
  checked_area input s self . output u64 . effects panic .
end

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end

fn rect.area input s rect . output u64 .
do
  return mul (field s w) (field s h) .
end

fn rect.grow input s rect . input k u64 . output rect .
do
  return make rect do w (add (field s w) k) . h (add (field s h) k) . end .
end

proc rect.checked_area input s rect . output u64 . effects panic .
do
  if eq (field s w) 0 . do panic "empty rect" . end .
  return mul (field s w) (field s h) .
end
```

적는 법:

- **서명마다 한 줄**이다. 줄은 op 의 **이름**으로 시작하고, 그 뒤에 op 머리와 같은 차례로 절을 적는다 —
  `input` 들, 그다음 `output`, 그다음 `effects`. 다음 이름이 나오면 다음 서명이다. 몇 개든 적는다.
- `self` 는 이 트레이트를 갖출 타입 자신이다. 갖추는 쪽에서는 그 자리에 실제 타입(`rect`)을 적는다.
- 갖추는 op 의 이름은 `<타입>.<이름>` 이다(`rect.area`). 타입 쪽에는 `satisfies shape .` 를 적는다.

**서명에는 `fn`/`proc` 을 적지 않는다.** 대신 `effects` 줄이 그 op 이 할 수 있는 일을 정한다.

| 서명의 `effects` | 갖추는 쪽 |
|---|---|
| 없음 | `fn` (또는 `effects` 를 적은 `proc`) |
| `effects panic` 처럼 효과가 있음 | 그 효과나 그보다 적은 효과를 적은 `proc` — 효과를 안 쓰면 `fn` 도 된다 |

`effects` 줄이 **없는** `proc` 은 입출력·할당·상태를 할 수 있다고 읽힌다. 그래서 효과 없는 서명을 그런
`proc` 으로 갖추면 `E-TRAIT-EFFECT` 로 거절된다 — 순수하면 `fn` 으로 적는다.

어긋나면 이렇게 말한다:

| 진단 | 뜻 |
|---|---|
| `E-TRAIT-UNDEF` | `satisfies` 가 없는 트레이트를 부른다 |
| `E-TRAIT-MISSING` | 목록의 op 하나가 없다 |
| `E-TRAIT-SIG` | op 은 있는데 매개변수 수·타입·출력이 다르다 |
| `E-TRAIT-EFFECT` | 갖춘 op 이 서명보다 많은 효과를 가진다 |
| `E-TRAIT-RECV` | 타입이 아니라 op 에 `satisfies` 를 적었다 |

☞ 트레이트는 상속이 아니다. 갖췄다는 것은 *"그 op 들이 있다"* 는 사실일 뿐, 다른 타입에서 물려받는 것은 없다.
자세한 규칙은 정본 §6.11.2 에 있다.

---

[← 목차](README.md) · [다음: 9. match →](09-match.md)
