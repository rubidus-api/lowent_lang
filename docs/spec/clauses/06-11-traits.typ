#import "../lib.typ": *

#sub("6.11", "타입에 붙은 op 과 트레이트 (Type-associated ops and traits)")[
  #sub("6.11.1", "타입에 op 을 붙이기")[
    #para("1")[
      op 의 이름 앞에 타입 이름과 점을 붙이면, 그 op 은 **그 타입에 붙은 것**이 된다.
    ]
    #para("2")[
      붙은 op 의 첫 입력은 그 타입의 값이다.
    ]
  ]
  #sub("6.11.2", "트레이트")[
    #para("1")[
      #t("트레이트", "trait") 는 어떤 타입이 갖춰야 할 op 의 **목록**이다. 각 op 의 이름과
      시그니처를 적는다.
    ]
    #plain[
      모양이 여럿이면(사각형 · 정사각형 …) 넓이를 구하는 **방법**은 타입마다 다르지만, *"넓이를 알려 준다"* 는
      **약속**은 같다. 트레이트는 그 약속에 이름을 붙인다. 그러면 *"넓이를 알려 주는 것이면 무엇이든"* 받는 op 을
      **한 번만** 쓸 수 있다 — 타입마다 같은 op 을 베껴 쓰지 않아도 되고, 약속을 안 지킨 타입은 번역 때 걸린다.
      부르는 자리는 그 타입이 무엇인지 몰라도 된다. 이 언어에는 상속이 없으므로, 여러 타입을 한 이름으로 다루는
      길은 이것이다.
    ]
    #ex("트레이트는 무엇에 쓰나 — 한 op 이 여러 타입을 받는다", "module ex_trait_why .

rem 모양마다 넓이를 구하는 법은 다르다. 그러나 «넓이를 알려 준다» 는 약속은 같다.
trait shape do
  area input s self . output u64 .
end

def struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end

def struct square do
  satisfies shape .
  side u64 .
end

fn rect.area input s rect . output u64 .
do
  return mul (field s w) (field s h) .
end

fn square.area input s square . output u64 .
do
  return mul (field s side) (field s side) .
end

rem 이 op 은 **어떤 모양이든** 받는다 — `requires shape t` 가 «넓이를 알려 주는 타입만» 이라고 못박는다.
fn double_area input comptime t type . input s t . output u64 .
  requires shape t .
do
  return mul 2 (method s area) .
end

fn demo output u64 .
do
  let r be lit rect do w 2 . h 3 . end .
  let q be lit square do side 4 . end .
  return add (double_area rect r) (double_area square q) .
end",
      out: "demo() = 44")
    #para("1a")[
      트레이트는 op 을 **여럿** 적을 수 있다. 서명 하나는 op 의 **이름으로 시작**하고 그 op 의 절이 뒤따르며,
      다음 이름이 다음 서명을 연다. 서명마다 한 줄에 적는 것이 관례다. 서명의 절은 op 머리와 **같은 차례**를
      따른다(#cref("6.4.1") (3a)) — 입력 · 출력 · 효과 차례다. 적는 op 의 수에는 한도가 없다.
    ]
    #para("1b")[
      서명에는 `fn`·`proc` 을 적지 **아니한다.** 그 op 이 무엇을 할 수 있는지는 서명의 `effects` 줄이 정한다.
      - `effects` 줄이 없으면 효과가 **없는** op 이다. 이것은 `fn` 으로도, 효과를 적은 `proc` 으로도 갖출 수 있다.
      `proc` 은 언제나 `effects` 줄을 적는다(#cref("7.1"), 없으면 `E-EFFECT-MISSING`) — 효과 없는 서명을 갖추는 `proc` 은
      `effects none .` 이다.
      - `effects` 줄이 있으면 갖추는 쪽은 그 효과 **또는 그보다 적은** 효과를 적은 `proc` 이거나, 효과가 없으면 `fn` 이다.
    ]
    #para("2")[
      트레이트 안에서 `self` 는 그것을 갖출 타입 자신을 가리킨다.
    ]
    #para("3")[
      타입이 `satisfies` 로 트레이트를 적으면, 처리기는 그 목록의 op 이 **정확히 그
      시그니처로** 있는지 검사한다. 하나라도 없거나 어긋나면 번역이 거부된다.
    ]
    #para("4")[
      어긋나는 갈래는 다섯이다.
    ]
    #tbl("트레이트를 갖추지 못한 자리")[
      #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
      [*진단*], [*무엇이 어긋났는가*],
      [`E-TRAIT-UNDEF`], [`satisfies` 가 **선언되지 않은** 트레이트를 부른다 — 아무것도 가리키지 않는 주장은 아무것도 검사하지 아니한다],
      [`E-TRAIT-MISSING`], [목록의 op 이 없다],
      [`E-TRAIT-SIG`], [op 은 있으나 **매개변수의 수**가 다르다 — 정확히 맞지 않는 트레이트는 계약을 실어 나르지 못한다],
      [`E-TRAIT-EFFECT`], [갖춘 쪽이 트레이트가 적지 **않은 효과**를 가진다. 부르는 쪽은 트레이트의 계약을 보고 판단하므로, 그 계약에 없는 일을 하면 판단이 틀린다],
      [`E-TRAIT-RECV`], [**op** 이 트레이트를 갖추겠다고 적었다 — 갖추는 것은 **타입**이다],
      )
    ]
    #para("5")[
      트레이트의 op 이 효과 줄에 `via self` 를 적으면, 갖추는 쪽은 그 op 에 **할당 계열 효과** —
      `alloc` · `heap` · `lock` · `atomic` — 를 트레이트보다 더 적을 수 있다. 그 밖의 효과를 더 적는
      것은 여전히 `E-TRAIT-EFFECT` 다. 더 적은 효과는 제네릭 op 이 `via <타입 매개변수>` 로 부르는
      쪽까지 나른다(#cref("7.1.1") (6)).
    ]
    #plain[
      트레이트가 하는 일은 「이 타입은 이런 것들을 할 줄 안다」를 **부르는 쪽이 믿을 수 있게**
      만드는 것이다. 그래서 이름만 맞고 효과가 다르면 그 믿음이 깨진다 — 효과까지 맞아야
      트레이트가 계약을 실어 나른다.
    ]
    #ex("트레이트를 갖춘다", "module ex_trait .

rem 트레이트는 타입이 갖춰야 할 op 의 목록이다.
trait shape do
  area input s self . output u64 . effects none .
end

rem `satisfies` 를 적으면 그 목록을 갖췄는지 검사받는다.
def struct rect do
  satisfies shape .
  w u8 .
  h u8 .
end

fn rect.area input s rect . output u64 .
do
  return mul (widen u64 (field s w)) (widen u64 (field s h)) .
end")
    #rejected("갖추겠다고 적고 안 갖추면", "module ex_trait_bad .

trait shape do
  area input s self . output u64 . effects none .
end

def struct rect do
  satisfies shape .     rem 갖추겠다고 적었는데
  w u8 .
  h u8 .
end

rem `rect.area` 를 만들지 않았다", "E-TRAIT-MISSING")
    #ex("op 을 여럿 가진 트레이트", "module ex_trait_many .

rem 서명마다 한 줄 — 이름으로 시작하고 입력 · 출력 · 효과 차례다.
trait shape do
  area input s self . output u64 .
  perimeter input s self . output u64 .
  grow input s self . input k u64 . output self .
  checked_area input s self . output u64 . effects panic .
end

def struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end

rem 효과 줄이 없는 서명은 `fn` 으로 갖춘다.
fn rect.area input s rect . output u64 .
do
  return mul (field s w) (field s h) .
end

fn rect.perimeter input s rect . output u64 .
do
  return mul 2 (add (field s w) (field s h)) .
end

fn rect.grow input s rect . input k u64 . output rect .
do
  return lit rect do w (add (field s w) k) . h (add (field s h) k) . end .
end

rem 효과를 적은 서명은 그 효과를 적은 `proc` 으로 갖춘다.
proc rect.checked_area input s rect . output u64 . effects panic .
do
  if eq (field s w) 0 . do panic \"empty rect\" . end
  return mul (field s w) (field s h) .
end")
    #rejected("효과 줄이 없는 proc 으로 효과 없는 서명을 갖추려 한다", "module ex_trait_proc_noeff .

trait shape do
  area input s self . output u64 .
end

def struct rect do
  satisfies shape .
  w u64 .
  h u64 .
end

proc rect.area input s rect . output u64 .
do
  return mul (field s w) (field s h) .
end", "E-TRAIT-EFFECT")
    #plain[
      트레이트는 *"이 타입은 이런 일을 할 수 있다"* 를 **검사받는 형태로** 적는 방법이다.
      주석으로 적으면 코드가 바뀌어도 그대로 남지만, `satisfies` 는 갖추지 못하는 순간
      번역이 거부된다.
    ]
    #caution("상속이 아니다")[
      트레이트는 타입 사이에 **위아래를 만들지 아니한다.** 어떤 타입이 트레이트를 갖췄다는
      것은 *"그 op 들을 갖고 있다"* 는 사실일 뿐이며, 다른 타입의 무엇을 물려받지 않는다.
      이 언어에는 상속이 없다.
    ]
  ]
  #sub("6.11.3", "붙은 op 을 부른다 — method")[
    #para("1")[
      `method` 는 값에 붙은 op 을 부르는 폼이다. 모양은
      `method <값> <마디> … <이름> <인자> … .` 이다.
    ]
    #para("2")[
      마디와 이름은 **타입을 따라가서** 갈린다. 수신자의 타입에서 마디를 하나씩
      내려가다가, 그 타입에 그 이름의 붙은 op 이 있으면 **거기가 이름**이고 그
      뒤는 전부 인자다. 그러므로 `method o inner area` 는 `o` 의 `inner` 에 붙은
      `area` 를 부르고, `method s plus 5` 는 `s` 에 붙은 `plus` 를 `5` 로 부른다.
    ]
    #para("3")[
      수신자는 이름일 필요가 없다. 다른 폼의 결과여도 되며, 그때 수신자의 타입은
      그 폼의 출력 타입이다 — 그래서 `method (method x grow 2) area` 가 성립한다.
    ]
    #para("4")[
      수신자의 타입을 정할 수 없으면 처리기는 프로그램을 거절한다. 붙은 op 은
      **타입으로** 찾는 것이므로, 타입을 모르면 부를 op 도 정해지지 않는다.
    ]
    #ex("붙은 op 을 부른다", "module ex_method .

def struct rect do
  w u64 .
  h u64 .
end

fn rect.area input s rect . output u64 .
do
  return mul (field s w) (field s h) .
end

export fn twice_area input s rect . output u64 .
do
  return mul 2 (method s area) .
end")
    #note[
      중위로 적는 표기(`s..area`) 는 **없다**. 그것은 언어에서 유일하게 오른쪽에서
      왼쪽으로 읽히는 자리였고, 다단 수신자를 적을 방법이 없었다. 폼으로 통일하면
      호출은 **언제나 이름이 먼저**다.
    ]
  ]
  #sub("6.11.4", "붙은 op 이 어긋나는 자리")[
    #para("1")[
      `method` 는 **수신자와 이름**을 모두 요구한다(`E-METHOD-RECV`).
    ]
    #para("2")[
      수신자의 타입에 그 이름의 붙은 op 이 없으면 거부된다(`E-METHOD-UNDEF`) —
      `fn <타입>.<이름>` 으로 선언되어 있어야 한다.
    ]
    #caution("")[
      붙은 op 은 **이름칸을 나누는 장치**이지 상속이 아니다(#cref("6.11.2")). 그러므로 없는
      이름을 부르면 위로 찾아 올라가는 일이 없고, 그 자리에서 바로 거부된다.
    ]
    #rejected("그 타입에 그 이름의 붙은 op 이 없다", "module ex_method_undef .

def struct p do
  x u8 .
end

fn f input s p . output u8 .
do
  return method s nosuch .
end", "E-METHOD-UNDEF")
  ]
]
