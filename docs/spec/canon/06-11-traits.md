## 6.11 타입에 붙은 op 과 트레이트 (Type-associated ops and traits)

## 6.11.1 타입에 op 을 붙이기

(1) op 의 이름 앞에 타입 이름과 점을 붙이면, 그 op 은 **그 타입에 붙은 것**이 된다.

(2) 붙은 op 의 첫 입력은 그 타입의 값이다.

## 6.11.2 트레이트

(1) ⟦트레이트|trait⟧ 는 어떤 타입이 갖춰야 할 op 의 **목록**이다. 각 op 의 이름과
      시그니처를 적는다.

> [!산문] 무엇에 쓰나
> 모양이 여럿이면(사각형 · 정사각형 …) 넓이를 구하는 **방법**은 타입마다 다르지만, *"넓이를 알려 준다"* 는
> **약속**은 같다. 트레이트는 그 약속에 이름을 붙인다. 그러면 *"넓이를 알려 주는 것이면 무엇이든"* 받는 op 을
> **한 번만** 쓸 수 있다 — 타입마다 같은 op 을 베껴 쓰지 않아도 되고, 약속을 안 지킨 타입은 번역 때 걸린다.
> 부르는 자리는 그 타입이 무엇인지 몰라도 된다. 이 언어에는 상속이 없으므로, 여러 타입을 한 이름으로 다루는
> 길은 이것이다.

```lowent 예제: 트레이트는 무엇에 쓰나 — 한 op 이 여러 타입을 받는다 · 결과: demo() = 44
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
  return mul field s. w . field s. h . . .
end .

fn square.area input s square . output u64 .
do
  return mul field s. side . field s. side . . .
end .

rem 이 op 은 **어떤 모양이든** 받는다 — `requires shape t` 가 «넓이를 알려 주는 타입만» 이라고 못박는다.
fn double_area input comptime t type . input s t . output u64 .
  requires shape t. . .
do
  return mul 2 method s. area. . . .
end .

fn demo output u64 .
do
  let r lit rect do w 2 . h 3 . end . .
  let q lit square do side 4 . end . .
  return add double_area rect. r. . double_area square. q. . . .
end .
```

(1a) 트레이트는 op 을 **여럿** 적을 수 있다. 서명 하나는 op 의 **이름으로 시작**하고 그 op 의 절이 뒤따르며,
      다음 이름이 다음 서명을 연다. 서명마다 한 줄에 적는 것이 관례다. 서명의 절은 op 머리와 **같은 차례**를
      따른다(⟦§6.4.1⟧ (3a)) — 입력 · 출력 · 효과 차례다. 적는 op 의 수에는 한도가 없다.

(1b) 서명에는 `fn`·`proc` 을 적지 **아니한다.** 그 op 이 무엇을 할 수 있는지는 서명의 `effects` 줄이 정한다.
      - `effects` 줄이 없으면 효과가 **없는** op 이다. 이것은 `fn` 으로도, 효과를 적은 `proc` 으로도 갖출 수 있다.
        `proc` 은 언제나 `effects` 줄을 적는다(⟦§7.1⟧, 없으면 `E-EFFECT-MISSING`) — 효과 없는 서명을 갖추는 `proc` 은
        `effects none .` 이다.
      - `effects` 줄이 있으면 갖추는 쪽은 그 효과 **또는 그보다 적은** 효과를 적은 `proc` 이거나, 효과가 없으면 `fn` 이다.

(2) 트레이트 안에서 `self` 는 그것을 갖출 타입 자신을 가리킨다.

(3) 타입이 `satisfies` 로 트레이트를 적으면, 처리기는 그 목록의 op 이 **정확히 그
      시그니처로** 있는지 검사한다. 하나라도 없거나 어긋나면 번역이 거부된다.

(4) 어긋나는 갈래는 다섯이다.

> [!표] 트레이트를 갖추지 못한 자리
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*진단*], [*무엇이 어긋났는가*],
> [`E-TRAIT-UNDEF`], [`satisfies` 가 **선언되지 않은** 트레이트를 부른다 — 아무것도 가리키지 않는 주장은 아무것도 검사하지 아니한다],
> [`E-TRAIT-MISSING`], [목록의 op 이 없다],
> [`E-TRAIT-SIG`], [op 은 있으나 **매개변수의 수**가 다르다 — 정확히 맞지 않는 트레이트는 계약을 실어 나르지 못한다],
> [`E-TRAIT-EFFECT`], [갖춘 쪽이 트레이트가 적지 **않은 효과**를 가진다. 부르는 쪽은 트레이트의 계약을 보고 판단하므로, 그 계약에 없는 일을 하면 판단이 틀린다],
> [`E-TRAIT-RECV`], [**op** 이 트레이트를 갖추겠다고 적었다 — 갖추는 것은 **타입**이다],
> )

(5) 트레이트의 op 이 효과 줄에 `via self` 를 적으면, 갖추는 쪽은 그 op 에 **할당 계열 효과** —
      `alloc` · `heap` · `lock` · `atomic` — 를 트레이트보다 더 적을 수 있다. 그 밖의 효과를 더 적는
      것은 여전히 `E-TRAIT-EFFECT` 다. 더 적은 효과는 제네릭 op 이 `via <타입 매개변수>` 로 부르는
      쪽까지 나른다(⟦§7.1.1⟧ (6)).

> [!산문]
> 트레이트가 하는 일은 「이 타입은 이런 것들을 할 줄 안다」를 **부르는 쪽이 믿을 수 있게**
> 만드는 것이다. 그래서 이름만 맞고 효과가 다르면 그 믿음이 깨진다 — 효과까지 맞아야
> 트레이트가 계약을 실어 나른다.

```lowent 예제: 트레이트를 갖춘다
module ex_trait .

rem 트레이트는 타입이 갖춰야 할 op 의 목록이다.
trait shape do
  area input s self . output u64 . effects none .
end .

rem `satisfies` 를 적으면 그 목록을 갖췄는지 검사받는다.
struct rect do
  satisfies shape .
  w u8 .
  h u8 .
end .

fn rect.area input s rect . output u64 .
do
  return mul widen u64 field s. w . . widen u64 field s. h . . . .
end .
```

```lowent-거부: 갖추겠다고 적고 안 갖추면 · E-TRAIT-MISSING
module ex_trait_bad .

trait shape do
  area input s self . output u64 . effects none .
end .

struct rect do
  satisfies shape .     rem 갖추겠다고 적었는데
  w u8 .
  h u8 .
end .

rem `rect.area` 를 만들지 않았다
```

```lowent 예제: op 을 여럿 가진 트레이트
module ex_trait_many .

rem 서명마다 한 줄 — 이름으로 시작하고 입력 · 출력 · 효과 차례다.
trait shape do
  area input s self . output u64 .
  perimeter input s self . output u64 .
  grow input s self . input k u64 . output self .
  checked_area input s self . output u64 . effects panic .
end .

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end .

rem 효과 줄이 없는 서명은 `fn` 으로 갖춘다.
fn rect.area input s rect . output u64 .
do
  return mul field s. w . field s. h . . .
end .

fn rect.perimeter input s rect . output u64 .
do
  return mul 2 add field s. w . field s. h . . . .
end .

fn rect.grow input s rect . input k u64 . output rect .
do
  return lit rect do w add field s. w . k. . . h add field s. h . k. . . end . .
end .

rem 효과를 적은 서명은 그 효과를 적은 `proc` 으로 갖춘다.
proc rect.checked_area input s rect . output u64 . effects panic .
do
  if eq field s. w . 0 . do panic "empty rect" . end .
  return mul field s. w . field s. h . . .
end .
```

```lowent-거부: 효과 줄이 없는 proc 으로 효과 없는 서명을 갖추려 한다 · E-TRAIT-EFFECT
module ex_trait_proc_noeff .

trait shape do
  area input s self . output u64 .
end .

struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end .

proc rect.area input s rect . output u64 .
do
  return mul field s. w . field s. h . . .
end .
```

> [!산문]
> 트레이트는 *"이 타입은 이런 일을 할 수 있다"* 를 **검사받는 형태로** 적는 방법이다.
> 주석으로 적으면 코드가 바뀌어도 그대로 남지만, `satisfies` 는 갖추지 못하는 순간
> 번역이 거부된다.

> [!주의] 상속이 아니다
> 트레이트는 타입 사이에 **위아래를 만들지 아니한다.** 어떤 타입이 트레이트를 갖췄다는
> 것은 *"그 op 들을 갖고 있다"* 는 사실일 뿐이며, 다른 타입의 무엇을 물려받지 않는다.
> 이 언어에는 상속이 없다.

## 6.11.3 붙은 op 을 부른다 — method

(1) `method` 는 값에 붙은 op 을 부르는 폼이다. 모양은
      `method <값> <마디> … <이름> <인자> … .` 이다.

(2) 마디와 이름은 **타입을 따라가서** 갈린다. 수신자의 타입에서 마디를 하나씩
      내려가다가, 그 타입에 그 이름의 붙은 op 이 있으면 **거기가 이름**이고 그
      뒤는 전부 인자다. 그러므로 `method o inner area` 는 `o` 의 `inner` 에 붙은
      `area` 를 부르고, `method s plus 5` 는 `s` 에 붙은 `plus` 를 `5` 로 부른다.

(3) 수신자는 이름일 필요가 없다. 다른 폼의 결과여도 되며, 그때 수신자의 타입은
      그 폼의 출력 타입이다 — 그래서 `method (method x grow 2) area` 가 성립한다.

(4) 수신자의 타입을 정할 수 없으면 처리기는 프로그램을 거절한다. 붙은 op 은
      **타입으로** 찾는 것이므로, 타입을 모르면 부를 op 도 정해지지 않는다.

```lowent 예제: 붙은 op 을 부른다
module ex_method .

struct rect do
  w u64 .
  h u64 .
end .

fn rect.area input s rect . output u64 .
do
  return mul field s. w . field s. h . . .
end .

export fn twice_area input s rect . output u64 .
do
  return mul 2 method s. area. . . .
end .
```

> [!참고]
> 중위로 적는 표기(`s..area`) 는 **없다**. 그것은 언어에서 유일하게 오른쪽에서
> 왼쪽으로 읽히는 자리였고, 다단 수신자를 적을 방법이 없었다. 폼으로 통일하면
> 호출은 **언제나 이름이 먼저**다.

## 6.11.4 붙은 op 이 어긋나는 자리

(1) `method` 는 **수신자와 이름**을 모두 요구한다(`E-METHOD-RECV`).

(2) 수신자의 타입에 그 이름의 붙은 op 이 없으면 거부된다(`E-METHOD-UNDEF`) —
      `fn <타입>.<이름>` 으로 선언되어 있어야 한다.

> [!주의]
> 붙은 op 은 **이름칸을 나누는 장치**이지 상속이 아니다(⟦§6.11.2⟧). 그러므로 없는
> 이름을 부르면 위로 찾아 올라가는 일이 없고, 그 자리에서 바로 거부된다.

```lowent-거부: 그 타입에 그 이름의 붙은 op 이 없다 · E-METHOD-UNDEF
module ex_method_undef .

struct p do
  x u8 .
end .

fn f input s p . output u8 .
do
  return method s. nosuch. . .
end .
```

## 6.11.5 실행 중에 고르기 — `object` 와 `dyn`

(1) 타입 `object <트레이트>` 의 값은 그 트레이트를 `satisfies` 로 갖춘 **어느 타입의 값이든** 담는다. 어느 타입인지는 실행 중의 값이 정하고,
      처리기는 번역할 때 그 타입을 모른다. 값은 `object <트레이트> <값> .` 으로 만든다 — 그 값을 **복사해 담는다**(원래 값을 가리키지 않는다 · 소유자 2026-11-20 — 빌려 담는 꼴은 두지 아니한다).
      `<값>` 의 타입은 적혀 있어야 한다: 타입을 적은 이름이거나 `lit <타입> …` 이다(타입은 짐작하지 아니한다 · `E-DYN-LIT`).
      그 타입이 트레이트를 갖추지 않았으면 `E-DYN-NOTSAT` 다.

(2) `dyn <값> <op 이름>. <인자> .` 는 `<값>` 에 든 타입이 갖춘 그 op 을 **실행 중에 골라** 부른다. `<값>` 은 `object <트레이트>` 로 적은
      이름이어야 하고(`E-DYN-RECV`), op 은 그 트레이트에 적힌 것이어야 한다(`E-DYN-OP`). 결과의 타입과 인자는 트레이트의 서명이 정한다.

(3) 실행 중에 op 을 고르는 일은 효과 `dyn` 이다(⟦§7.1⟧). `dyn` 을 부르는 op 은 효과 줄에 `dyn` 을 적은 `proc` 이어야 하고
      (`E-DYN-EFFECT`), 그 op 을 부르는 쪽으로 다른 효과처럼 번진다. 그래서 어느 op 이 실행 중에 고르는지가 서명에 보인다.
      고른 op 이 하는 일의 효과는 트레이트 서명이 적은 효과를 넘지 아니한다(⟦§6.11.2⟧ (1b) · `E-TRAIT-EFFECT`).

(4) `object` 로 쓸 수 있는 트레이트는 op 마다 다음을 지켜야 한다 — 어기면 `E-DYN-TRAIT` 다.
      - 첫 입력이 `self` 다(고르는 기준이 되는 값). `self` 는 다른 입력과 출력에 오지 아니한다 — 그 크기는 실행 중에야 정해진다.
      - `comptime` 입력이 없고, 효과 줄에 `via self` 가 없다.
      - 나머지 입력과 출력은 op 의 입력 · 출력에 적을 수 있는 타입이다 — `u64` · `slice u8` · `mut slice u8` · `option u64` · `vec 4 u32` 와, 입력에는
        `array 3 u8` · `matrix 2 2 3 u32` 도(⟦§6.2.6⟧ (1b) · ⟦§6.2⟧ matrix (3)). 길이와 모양은 갖춘 타입의 op 이 진입에서 검사한다.

(5) 트레이트를 갖춘 타입이 하나도 없으면 `E-DYN-NOIMPL` 이다. 갖춘 타입은 **어느 모듈에 있어도 된다** — 다만 `object <트레이트>` 를 쓰는 모듈은
      트레이트의 모듈과 갖춘 타입의 모듈을 모두 `use` 해야 한다(C 가 머리글을 들여오듯 · 서로 `use` 해도 된다). 처리기는 그것들을 모두 보는 모듈에
      고르는 표를 짓는다 — 트레이트의 모듈이 다 보면 거기, 아니면 `object` 를 쓰는 첫 모듈. 들여오지 않은 모듈이 있거나, `object` 를 쓰는 다른 모듈이
      표를 지은 모듈을 `use` 하지 않았으면 `E-DYN-MODULE` 이다(무엇을 `use` 하라고 말한다).
      `object <트레이트>` 의 값으로 원래의 타입을 되찾는 길은 없다 — 그 값의 칸은 `field` 로 읽지 못한다(`E-DYN-FORM`).
      `dyn` 과 `object` 의 꼴이 틀리면(op 이름 뒤에 점이 없다 · 값이 둘이다) `E-DYN-FORM` 이다. 처리기는 그 값의 타입에 `<트레이트>__object` 라는
      이름을 주므로, 저자가 같은 이름을 지으면 `E-DYN-NAME` 이다.

(5a) `object <트레이트>` 는 C 경계와 액터를 **다른 구조체와 같은 길로** 건넌다 — C 에는 값으로(칸 차례 그대로 · 머리글에 구조체로 실린다),
      액터에는 메시지의 복사로. 고르는 일(`dyn`)은 이쪽에서만 한다. C 가 부르는 op 의 `object` 입력은 **진입에서 번호를 검사한다** —
      처리기가 `requires` 를 붙이고, 갖춘 타입의 번호가 아니면 거기서 멈춘다. 우리가 부르는 C 함수는 `object` 를 돌려주지 못한다(`E-DYN-FFI`) —
      그 번호를 출구에서 검사할 길이 아직 없다. C 가 채우게 하려면 이쪽이 만든 값을 넘긴다.

(6) `object <트레이트>` 는 크기가 정해진 **값 타입**이다. 그 값은 갖춘 타입의 번호 하나와 갖춘 타입 **모두**의 칸을 나란히 지닌다 —
      크기는 그 번호와 칸들을 나란히 둔 구조체의 크기다 — 칸들의 합에 번호와 맞춤 여백을 더한 것이고, 가장 큰 갖춘 타입이 아니다. 그래서 구조체처럼 원소 나열(⟦§6.2.6⟧ (6) · `lit slice object <트레이트> … .` ·
      `lit array <n> object <트레이트> … .`)의 원소가 되고, `for` 로 돌 때 원소의 이름도 `object <트레이트>` 다.
      남의 칸은 0 으로 채우므로 갖춘 타입의 칸은 0 을 적을 수 있는 것이어야 한다: 크기 있는 수 · 부동 · `bool`(0 · `0.0` · `false`),
      `array <n> <수>`(`_`), `slice <타입>`(빈 조각 · ⟦§6.2.6⟧ (6a-1)), 그리고 그런 칸들의 구조체(펴서 나란히 둔다). 열거 · `owned` · 참조 칸은
      `E-DYN-FIELD` 다. 슬라이스 칸과 배열 칸을 함께 지닌 값은 목록에 담지 못한다 — 구조체의 나열이 그 둘을 함께 놓지 못하는 것과 같다(⟦§6.2.6⟧ (6)).

> [!산문] 무엇에 쓰나
> 트레이트 경계(⟦§6.11.2⟧)로 부르면 처리기가 번역 때 타입마다 op 을 따로 지어 간접 부름이 없다 — 가장 빠르다.
> 갈래 있는 열거(⟦§6.6⟧)는 타입의 집합을 **저자가 닫는다**. `object` 는 그 둘이 안 되는 자리 — 값의 타입이 실행 중에 정해지고,
> 그 집합을 부르는 쪽이 적지 않는 자리 — 를 위한 것이다. 그 대신 고르는 값을 치르고(간접 부름은 펼 수 없다), 그 사실이 `dyn` 이라는 낱말과
> 효과에 보인다.

```lowent 예제: 실행 중에 고른다 — 같은 op 이 사각형도 정사각형도 받는다 · 결과: demo() = 28
module ex_dyn .

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
  return mul field s. w . field s. h . . .
end .

fn square.area input s square . output u64 .
do
  return mul field s. side . field s. side . . .
end .

rem 이 op 은 받는 값이 무슨 타입인지 모른다 — `dyn` 이 실행 중에 고른다
proc twice input o object shape . output u64 . effects dyn .
do
  return mul 2 dyn o. area. . . .
end .

proc demo output u64 . effects dyn .
do
  let r rect lit rect do w 2 . h 3 . end . .
  let q square lit square do side 4 . end . .
  let a object shape object shape r. . .
  let b object shape object shape q. . .
  return add twice a. . dyn b. area. . . .
end .
```

```lowent-거부: 실행 중에 고르면서 효과 줄에 `dyn` 을 적지 않았다 · E-DYN-EFFECT
module ex_dyn_effect .

trait shape do
  area input s self . output u64 .
end .

struct rect do
  satisfies shape .
  w u64 .
end .

fn rect.area input s rect . output u64 .
do
  return field s. w . .
end .

proc f input o object shape . output u64 . effects none .
do
  return dyn o. area. . .
end .
```
