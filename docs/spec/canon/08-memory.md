# 8 메모리와 소유 (Memory and ownership)

(1) 이 조항은 값이 **어디에 살고 언제 사라지는가**를 정한다. 로우엔트에는 쓰레기 수집기가
      없고, 손으로 해제하다 생기는 잘못도 없다 — 그 둘 대신 번역 시점의 규칙이 있다.

## 8.1 값이 사는 곳

(1) **저장되는 값**은 어느 ⟦영역|region⟧ 에 산다. 영역은 타입과 함께 정해지며
      소스에 적혀 있다.

(1a) 모든 값이 영역을 갖는 것은 아니다. 계산 중에만 있는 작은 값은 저장될 자리를 갖지
      않을 수 있으며, 그런 값에는 영역이 없다. 영역은 **값이 어디에 저장되는가**를 말하는
      것이지 값이 존재하는 방식을 말하는 것이 아니다.

(2) 저장되는 값의 영역은 세 갈래다.

> [!표] 값이 사는 곳
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*갈래*], [*설명*],
> [지역], [op 안에서 나고, op 이 끝나면 사라진다. 가장 흔하다],
> [정적], [프로그램이 사는 동안 계속 있다],
> [얻은 것], [뿌리에서 얻는다. 누가 돌려줄지가 소유로 정해진다(#cref("8.4"))],
> )

(2a) 얻는 뿌리는 **둘**이다. 둘은 따로 깎이고 따로 되감긴다.

> [!표] 두 뿌리
> #table(columns: (auto, auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*뿌리*], [*무엇으로 깎나*], [*효과*], [*성질*],
> [고정 창], [`cap allocator` · `heap` 이 아닌 영역], [`alloc`], [**자라지 않는다.** 다 쓰면 `none` 이다. 운영체제가 없는 기계에서는 링커가 주는 두 경계 사이다 — 크기가 실행 파일에 박히지 않고, 기기가 부팅할 때 그 자리가 쓰인다],
> [힙], [`cap heap` · `region <이름> heap`], [`heap`], [**자란다.** 실행 중에 더 받아 오며, 이미 준 바이트를 옮기지 아니한다. 운영체제가 있는 기계에만 있다],
> )

(3) 값이 어느 영역에 사는지는 **소스에 적혀 있다.** 처리기가 몰래 옮기지 아니한다.

(3a) 뿌리에서 얻는 것 말고도, `some`·`ok`·`make`·`spawn actor` 가 만드는 값은 처리기의 **유한 풀**에 산다. 이
      풀은 그 op 의 암묵 ⟦frame⟧ 이다 — 효과가 없고, `alloc`·`heap` 을 적지 않으며, 권한이 필요 없다. 풀은 루프를 한
      바퀴 돌 때마다 되감기고, 동시에 살아 있는 값이 풀의 크기를 넘으면 그 자리에서 **멈춘다**(값이 아니라 중단이다).
      크기는 기계가 정하며(운영체제가 없는 기계는 작다) 짓는 사람이 조절할 수 있다. 이 크기는 규범이 아니라 처리기가
      밝히는 사실이다.

(3b) 뿌리의 커서는 실행 흐름 사이에 나뉘지 아니한다. 그러므로 태스크로 띄우는 op 은 뿌리에서 얻지 못하며
      (`E-ALLOC-TASK`), 태스크에 얼로케이터를 건네려면 그 얼로케이터의 `reserve` 가 커서를 원자적으로 옮겨야
      한다(`atomic` 효과 — `E-ALLOC-SHARED`).

> [!산문]
> 흔히 「스택」과 「힙」이라 부르는 것이 각각 지역과 얻은 것에 해당한다. 이름을 달리
> 쓰는 이유는, 이 언어에서는 그것이 기계의 구조가 아니라 **값의 성질**이기 때문이다.

## 8.2 영역을 받는 자리

(1) 영역은 **인자로 건네받는다**. `input <이름> region <타입> . .` 으로 적으며, 뒤의
      타입은 그 영역을 부르는 이름이다.

(2) 영역에서 자리를 얻는 op 은 `alloc` 효과를 갖는다(⟦§7.1⟧). 곧 **몰래 할당하는
      op 이 없다** — 자리를 쓰는 op 은 계약에 그렇게 적혀 있다.

(3) 영역에서 얻은 것은 그 영역보다 오래 살 수 없다. 영역이 끝나면 거기서 얻은 것도 끝난다.

(4) 영역은 낱낱이 돌려주지 아니한다. 영역이 끝날 때 **한꺼번에** 걷힌다.

```lowent 예제: 영역을 받아 자리를 얻는다
module ex_region .

type scratch u64 . .

proc build input temp region scratch . . output u64 . effects alloc .
do
  let s stack u64 . be stack_new temp capacity 4 . .
  push s 10 .
  push s 20 .
  return 2 .
end
```

> [!산문]
> `effects alloc` 이 적혀 있는 것에 주의한다. 자리를 얻는 것은 **효과**이므로, 순수한
> op(`fn` 에 `effects none`)은 영역을 쓸 수 없다. 어디서 메모리가 나오는지가 서명에
> 적혀 있다는 뜻이다.

(4) 영역을 여는 문은 이름과 **종류**를 함께 적는다: `region <이름> <종류> do … end`.
      종류는 닫힌 여덟이다 — `stack` · `frame` · `arena` · `static` · `heap` · `mmap` ·
      `disk` · `device`. 그 밖의 낱말은 적합하지 아니하다(`E-REGION-KIND`).

(4a) 영역 안에서 얻은 값을 그 영역 **밖으로 들고 나가는** 것은 적합하지 아니하다
      (`E-REGION-ESCAPE`). 영역이 닫히면 그 값은 없으므로, 밖에 남은 이름은 없는 것을
      가리킨다. 들고 나가는 자리는 `return`, 영역 밖 이름에 대입하기, 그리고 영역 밖 이름의
      **칸이나 원소에** 대입하기(`set (field h store) b .`)다. 영역의 바이트를 들지 않는 값 —
      정수·참거짓처럼 스칼라 타입으로 묶인 것, `len`·`index` 처럼 스칼라를 내는 식 — 은
      들고 나가지 아니한다.

```lowent-거부: 영역의 슬라이스를 바깥 묶음의 칸에 넣는다 · E-REGION-ESCAPE
module ex_region_field .

struct holder do store mut slice u8 . . end .

proc f output u64 . effects alloc . do
  var h holder be make holder do store (subslice "abcd" 0 0) . end .
  region r arena do
    let g option mut slice u8 . . be alloc_bytes r capacity 16 .
    if is_some g . do
      let b mut slice u8 . be some_value g .
      set (field h store) b .
    end .
  end .
  return len (field h store) .
end .
```

(4b) 그 op 의 영역 매개변수가 아닌 이름으로 영역을 여는 것도 적합하지 아니하다
      (`E-REGION-UNDEF`).

(4c) 같은 뿌리(⟦§8.1⟧ (2a))의 영역이 안쪽에 열려 있는 동안 **바깥 출처의 이름으로 깎는** 것은
      적합하지 아니하다(`E-ALLOC-NESTED`). 바깥 영역 블록의 이름도, 영역 매개변수도, 같은 뿌리의
      권한(`cap allocator` 는 고정 창 · `cap heap` 은 힙)도 그렇다. 뿌리마다 깎는 자리가 하나라서
      안쪽 영역이 끝날 때 그 바이트까지 되감기기 때문이다. 다른 뿌리의 영역은 안쪽에 열려 있어도
      상관없다.

```lowent-거부: 안쪽 영역이 열린 채 바깥 영역에서 깎는다 · E-ALLOC-NESTED
module ex_region_nested .

proc f output u64 . effects alloc . do
  region outer arena do
    region inner arena do
      let g option mut slice u8 . . be alloc_bytes outer capacity 8 .
    end .
  end .
  return 0 .
end .
```

(4d) 영역 안에서 얻은 바이트를 **영역 밖에서 태어난 actor** 에게 보내는 것은 적합하지 아니하다
      (`E-ALLOC-OUTLIVES`). actor 는 영역보다 오래 살고, 받은 슬라이스를 간직하는지는 번역할 때
      보이지 않는다. actor 를 영역 안에서 만들거나, 영역보다 오래 사는 메모리를 건넨다.

(4e) 영역 블록을 나가는 **모든 길** — 끝에 닿기, `return`, 영역을 감싼 루프의 `break`·`continue` —
      에서 그 영역은 되감긴다. 영역 블록의 이름을 인자로 건네면 받은 쪽은 그것을 영역 매개변수로
      받아 같은 뿌리에서 깎는다.

(5) 종류는 **어느 뿌리에서 깎는가**를 정한다(⟦§8.1⟧ (2a)): `heap` 은 힙에서, 나머지 일곱은
      고정 창에서 깎고 되감는다. 두 뿌리의 영역은 한 op 안에서 겹쳐 열 수 있으며, 안쪽 영역을
      되감아도 다른 뿌리의 바깥 값은 남는다. 이 판의 처리기는 `heap` 이 아닌 일곱을 **서로
      다르게 다루지는 아니한다** — 그 일곱은 **어디서 저장소가 오는지**를 읽는 사람에게 말하는
      낱말이며, 처리기가 그것을 가려 쓰는 것은 뒷판의 일이다.

> [!참고]
> **어휘를 닫는 것과 동작을 가르는 것은 다른 일이다.** 아무 낱말이나 쓸 수 있으면 그
> 낱말은 아무것도 말해 주지 못한다 — 여덟으로 닫았기에 `region t arena` 를 읽은 사람이
> *"범프 할당이고 한꺼번에 되돌린다"* 를 안다. 동작의 구별은 **뿌리 둘**(고정 창 · 힙)까지
> 있고, 나머지 일곱 사이의 구별은 아직 없다. 그 사실을 여기 적어 둔다.

## 8.3 옮기기와 베끼기

(1) 값을 다른 이름에 넣을 때, 그 값이 소유를 가진 것이면 **옮겨진다**(원래 이름은 더는
      쓸 수 없다). 소유가 없는 값이면 베껴진다.

(2) 옮겨진 뒤에 원래 이름을 쓰면 번역이 거부된다.

## 8.3.1 값이 놓이는 자리 — 임시값과 목적지

(1) 값을 만드는 식이 **놓일 자리**(⟦목적지|destination⟧)를 가질 때가 있다. `let` 의 초기값,
      `return` 의 값, 인자 자리, 묶음의 필드가 그러하다.

(2) 다음 셋이 모두 성립하면 그 값은 **목적지에 곧바로 만들어진다** — 중간 임시값도, 그것을
      옮기는 일도 일어나지 아니한다.

> [!표] 곧바로 만들어지는 세 조건
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*조건*], [*뜻*],
> [목적지가 **비어 있다**], [`let` 초기화 · 아직 안 채운 필드 · 옮겨져 빈 자리],
> [값이 **도중에 실패하지 아니한다**], [목적지에 닿기 전에 빠져나가는 길이 없다],
> [목적지가 **그 값만의 것**이다], [같은 자리를 두 값이 다투지 아니한다],
> )

(3) 조건이 안 맞으면 ⟦임시값|temporary⟧ 이 만들어진다. 임시값은 그것을 감싸는 가장 작은
      범위에 놓이고, 그 범위가 끝날 때 **만든 차례의 거꾸로** 없어진다.

(4) 임시값을 가리키는 참조는 그 범위 밖으로 나갈 수 없다(⟦§8.4.1⟧).

(5) 값을 옮기는 별도의 연산은 없다. 옮기기는 **자리를 바꾸는 일**이지 값을 만들어 내는
      일이 아니다.

> [!참고]
> 왜 «곧바로 만들어진다» 를 규범으로 적는가. *"할 수 있으면 한다"* 로 두면 같은 소스가
> 처리기에 따라 복사를 하기도 하고 안 하기도 한다 — 그러면 **비용이 소스에서 안 보인다**.
> 위 세 조건은 읽는 사람이 소스만 보고 셀 수 있는 것들이라, 조건이 맞으면 복사가 **없다고
> 믿어도 된다**.

> [!참고]
> 씨 플러스 플러스(C++)는 이 자리를 «값 범주» 와 «옮김 생성자» 로 푼다. 로우엔트는 그
> 어휘를 들이지 아니한다 — 옮기기가 **자리의 일**이지 타입의 일이 아니기 때문이다.
> 그래서 옮김을 위한 특별한 선언도, 그것을 부르는 규칙도 없다.

## 8.4 참조

(1) ⟦참조|reference⟧ 는 다른 곳에 있는 값을 가리킨다. 두 갈래가 있다.

> [!용어] ref (shared reference)
> 읽기 참조. 여럿이 동시에 가질 수 있다. 이것으로는 값을 고칠 수 없다.

> [!용어] mut_ref (exclusive reference)
> 쓰기 참조. 한 값에 대해 **한 번에 하나만** 있을 수 있으며, 그동안 읽기 참조도 있을 수
> 없다.

(2) 이 규칙을 어기면 번역이 거부된다.

## 8.4.1 참조는 자기가 가리키는 것보다 오래 살 수 없다

(1) 지역 값을 가리키는 참조가 그 값이 사라진 뒤에도 남는 것을 ⟦탈출|escape⟧ 이라
      한다. 로우엔트는 탈출을 **번역 시점에 거부한다**.

(2) 곧 op 안에서 만든 값을 가리키는 참조를 그 op 밖으로 돌려줄 수 없다.

```lowent 예제: 읽기 참조는 여럿이 함께 가질 수 있다
module ex_ref .

export fn sum_two input a ref u32 . input b ref u32 . output u32 .
  requires le (deref a) 1000 .
  requires le (deref b) 1000 .
do
  return add (deref a) (deref b) .
end
```

```lowent-거부: 지역 값을 가리키는 참조를 돌려줄 수 없다 · E-ESCAPE: reference to a local escapes the op (dangling)
module ex_escape .

export fn leak output ref u32 .
do
  let here u32 be 42 .
  return ref here .     rem `here` 는 이 op 이 끝나면 사라진다
end
```

> [!산문]
> 위 코드가 만약 번역된다면, 돌려받은 참조는 **이미 사라진 값**을 가리킨다. 그 참조를
> 읽으면 무슨 값이 나올지 아무도 모른다. 그래서 이 언어는 그 코드를 아예 받아들이지
> 않는다 — 실행해 보고 아는 것이 아니라 **번역할 때** 안다.

> [!주의] 널 참조가 없다
> 참조는 언제나 살아 있는 값을 가리킨다. *"가리키는 것이 없음"* 을 나타내야 하면
> `option` 을 쓴다(⟦§6.2.8⟧) — 그러면 꺼내기 전에 확인하도록 처리기가 강제한다.

> [!참고]
> 읽기 여럿과 쓰기 하나를 가르는 규칙은 두 가지를 한꺼번에 막는다. 하나는 **다른 흐름이
> 동시에 고치는 것**이고, 다른 하나는 **읽는 중에 값이 바뀌는 것**이다. 둘 다 소스를
> 읽어서는 알 수 없는 종류의 잘못이라, 규칙으로 없앤다.

## 8.5 소유와 없애기

(1) ⟦소유|ownership⟧ 은 어떤 값을 없앨 책임이 누구에게 있는가를 말한다.
      `owned t` 는 소유를 가진 값이다.

(2) 소유를 가진 값은 **정확히 한 번** 없애져야 한다. 두 번 없애는 것도, 없애지 않고
      버리는 것도 번역 시점에 거부된다.

(3) 값을 없애는 일은 **두 갈래**이며, 이 언어는 그 둘을 다르게 다룬다.

> [!용어] 해제 (release)
> 메모리를 돌려주는 것처럼 **언제나 성공하고 기다리지 않는 일**. 실패가 없으므로
> 값의 수명이 끝나는 자리에서 **조용히 일어나도 된다**. 삼킬 실패가 없기 때문이다.

> [!용어] 완결 (completion)
> 파일 닫기·버퍼 비우기처럼 **실패할 수 있고 기다릴 수 있는 일**. 조용히 일어나면
> **그 실패를 건네줄 자리가 없다.** 그래서 반드시 저자가 적어서 불러야 한다.

(4) 어떤 값이 완결을 요구하는지는 **프로그램 자신의 선언이 정한다** — 그 타입을 값으로
      받아 `result` 를 돌려주는 op 이 있으면, 그것은 *"이걸 끝내는 일은 실패할 수 있다"* 는
      선언이다.

(5) 완결이 필요한 값을 부르지 않고 버리면 적합하지 아니하다. 처리기는 진단을 낸다
      (`E-OWN-INCOMPLETE`).

(6) 옮긴 값을 다시 쓰는 것도 적합하지 아니하다(`E-OWN-MOVED`).

(7) 완결이 필요한 값을 묶을 때는 **그 자리에 `owned` 라고 적어야** 한다
      (`E-OWN-BARE`). 그 낱말이 없으면 아무것도 끝내기를 요구하지 아니하며, 타입이
      말하는 것과 묶음이 말하는 것이 갈린다.

(8) 갈래가 나뉘었다가 다시 만나는 자리에서, 소유의 상태는 **모든 길에서 같아야** 한다
      (`E-OWN-JOIN`). 한 길에서 없애고 다른 길에서 살려 두면, 만난 뒤의 그 값이 살아
      있는지 아닌지를 **아무도 말할 수 없다.**

(9) 묶음의 `owned` 칸 하나를 옮긴 뒤에 **묶음 전체를 다시 옮기는** 것은 적합하지
      아니하다(`E-OWN-PARTIAL`). 받는 쪽은 온전한 묶음을 받았다고 여기는데 그 안의
      한 칸은 이미 남의 것이다.

> [!산문]
> 이 셋은 모두 같은 물음의 다른 얼굴이다 — *"지금 이 값을 없앨 책임이 누구에게 있는가"* 에
> **언제나 하나의 답**이 있어야 한다는 것. 답이 갈래마다 다르거나(8), 일부만 옮겨 갔거나(9),
> 애초에 묻지 않았다면(7) 그 값의 끝은 아무도 책임지지 않는 자리가 된다.

```lowent 예제: 해제 — 실패할 수 없으므로 `drop` 이면 된다
module ex_own .

type buffer u8 . .

fn sink input h owned buffer . output u8 .
do
  drop h .
  return 0 .
end
```

```lowent-거부: 두 번 없앨 수 없다 · E-OWN-MOVED
module ex_own_bad .

type buffer u8 . .

fn twice input h owned buffer . output u8 .
do
  drop h .
  drop h .     rem 이미 없어진 것을 또 없앤다
  return 0 .
end
```

> [!산문]
> 두 갈래를 가르는 기준이 하나뿐이라는 점이 중요하다 — **끝내는 일이 실패할 수 있는가.**
> 실패할 수 없으면 언어가 알아서 치워도 아무 정보가 사라지지 않는다. 실패할 수 있으면
> 그 실패를 누군가 받아야 하고, 받을 사람은 저자뿐이다.

> [!참고]
> 새 낱말을 만들지 않은 것도 눈여겨볼 자리다. 완결은 **보통의 op** 이다 — 소유한 값을
> 받고 `result` 를 돌려주는 함수. 그 모양 자체가 선언이 되므로 언어에 낱말을 더할
> 이유가 없었다.

> [!산문]
> 「없애기를 잊었다」를 번역 시점에 잡는다는 것이 이 조항의 핵심이다. 프로그램이 끝날 때
> 운영체제가 치워 주기를 기대하지 않으므로, 운영체제가 없는 환경에서도 같은 코드가
> 성립한다(⟦§5.1⟧).

## 8.6 메모리 안전이 어디까지 보장되는가

(1) 이 문서는 보장의 **강도를 갈라 적는다.** 그렇게 하지 않으면 지키지 못할 약속을 하게
      된다.

> [!표] 보장의 강도
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*강도*], [*뜻*],
> [증명됨], [기계로 검사된 증명이 있다],
> [정적], [번역 시점의 규칙으로 막는다. 규칙 자체의 증명은 아직 없다],
> [동적], [실행 중에 검사한다. 어기면 트랩한다],
> )

(2) 참조의 배타 규칙, 탈출 금지, 소유의 한 번 없애기는 **정적**이다.

(3) 슬라이스 경계와 계약은 **동적**이되, 처리기가 증명하면 검사가 사라진다(⟦§6.4.6⟧).

> [!주의] 「안전하다」는 말은 무엇이 어떻게 안전한지 밝혀야 뜻이 있다
> 어떤 언어가 *"메모리 안전"* 을 내세울 때, 그것이 번역 시점에 막는 것인지 실행 중에
> 검사하는 것인지, 증명된 것인지 규칙일 뿐인지가 갈린다. 이 문서가 강도를 갈라 적는
> 이유는 **못 지킬 약속을 하지 않기 위해서**다.

## 8.7 바이트를 다시 읽기 — `bit_cast` 와 `view`

(1) 같은 바이트를 **다른 타입으로 읽는** 일은 이 언어에서 두 갈래로 나뉜다. 갈래를
      나누는 까닭은, 그 일을 통째로 「안전하지 않다」로 미루면 해악이 어디까지 번지는지
      아무도 말할 수 없게 되기 때문이다.

> [!표] 바이트를 다시 읽는 두 갈래
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*갈래*], [*op*], [*무엇인가*],
> [값의 비트를 그대로 옮긴다], [`bit_cast`], [폭이 같은 두 스칼라 사이. 비트열은 그대로 두고 **읽는 법만** 바꾼다],
> [바이트 위에 짜임을 얹는다], [`view` · `try_view` · `view_array`], [바이트 조각을 어떤 짜임의 배치로 **읽는다**. 베끼지 아니한다],
> )

## 8.7.1 `bit_cast` — 비트는 그대로, 읽는 법만

(2) `bit_cast` 의 목표 타입은 **`plain` 인 스칼라**여야 한다. `plain` 이란 **모든 비트열이
      쓸모 있는 값이고 덧댐이 없는** 타입을 말한다.

(3) `bool` 과 ⟦열거|enum⟧ 은 `plain` 이 **아니다** — 쓸모없는 비트열(⟦덫 표현|trap
      representation⟧)이 있기 때문이다. 그러므로 이 둘은 이 갈래 밖이며 거부된다
      (`E-TYPE-BITCAST`).

> [!주의]
> 「`plain` 인가」는 **직접** 물어야 한다. 폭 표에 없는 것을 `plain` 이 아니라고 에둘러 묻는
> 방식은 폭 표가 채워지는 날 조용히 뒤집힌다. 우연히 성립하던 판정은 그 우연이 사라질 때
> 아무 말 없이 사라진다.

```lowent-거부: `bool` 은 덫 표현이 있어 `plain` 이 아니다 · E-TYPE-BITCAST
module ex_bitcast .

fn f input a u8 . output bool .
do
  return bit_cast bool a .     rem 모든 비트열이 참·거짓인 것은 아니다
end
```

## 8.7.2 `view` — 배치를 얹는다

(4) `view` 는 바이트 조각 위에 짜임의 배치를 얹는다. **베끼지 아니한다** — 그 자리의
      바이트를 그대로 읽는다.

(5) 얹으려면 두 가지가 맞아야 한다.

> [!표] `view` 가 서기 위한 조건
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*조건*], [*어기면*], [*왜*],
> [조각이 배치만큼 길다], [`E-VM-VIEW`], [모자란 바이트를 읽는 것은 이 언어가 막는 바로 그것이다],
> [시작 주소가 `align` 을 지킨다], [`E-VM-ALIGN`], [`align n` 은 **계약**이며, 계약을 검사하는 자리가 바로 이 경계다],
> )

(6) `try_view` 는 같은 일을 하되, 서지 못하면 **거짓말 대신 없음**을 준다(⟦option⟧). 곧
      실패가 값으로 돌아온다.

(7) `view_array` 는 같은 짜임을 **여럿** 얹는다. 조건은 같다.

> [!산문]
> 정렬을 「빠르기 문제」로 여기면 어긴 자리가 조용히 지나가고, 어떤 기계에서만 어느 날
> 무너진다. 이 언어는 그것을 **계약**으로 부르고 경계에서 검사한다 — 짧은 조각을 거절하는
> 것과 **정확히 같은 이유**로 어긋난 주소를 거절한다. 둘 다 「없는 것을 있는 것처럼
> 읽는 일」이다.

## 8.8 고칠 수 있음은 **적힌 대로만** 흐른다

(1) 조각과 참조는 기본이 **읽기**다. 고치려면 그 자리에 `mut` 이 적혀 있어야 한다.

> [!표] 고칠 수 있음이 어긋나는 자리
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*진단*], [*무엇이 어긋났는가*],
> [`E-TYPE-MUT`], [`mut` 이라 적히지 않은 조각의 원소에 쓴다],
> [`E-TYPE-REF`], [나누어 가진 읽기 참조를 통해 쓴다 — 고치려면 `mut_ref` 여야 한다],
> [`E-TYPE-ARGMUT`], [읽기만 되는 값을 `mut`·`owned`·`mut_ref` 를 받는 자리에 건넨다],
> [`E-TYPE-RETMUT`], [고칠 수 있는 것을 내놓겠다고 적고 읽기만 되는 것을 돌려준다],
> )

(2) 곧 고칠 수 있음은 **한 방향으로만** 좁아진다. 고칠 수 있는 것을 읽기로 건네는 것은
      되지만, 읽기만 되는 것을 고칠 수 있는 자리에 건네는 것은 되지 아니한다.

> [!산문]
> 이 규칙이 지키는 것은 **읽는 쪽의 믿음**이다. 어떤 값을 읽기로 받았다면 그것이 내가
> 보는 동안 바뀌지 않는다고 믿을 수 있어야 하고, 그 믿음은 아무도 몰래 쓰기 권한을
> 얻지 못할 때에만 선다. 그래서 좁아지는 방향은 하나뿐이다.

## 8.9 이름이 같아도 타입이 다르다

(1) 표현이 같아도 **이름이 다르면 다른 타입**이다(`E-TYPE-NOMINAL`). `newtype` 으로
      가른 것들, 그리고 크기가 같은 서로 다른 정수 타입이 그렇다.

(2) 선언되지 않은 타입 이름은 거부된다(`E-TYPE-UNDEF`).

(3) 정수와 부동소수는 **저절로 섞이지 아니한다**(`E-TYPE-MIX`). 건너려면 그렇게 적힌
      op 을 쓴다.

(4) 타입이 자기 자신을 품는 것은 거부된다(`E-TYPE-CYCLE`). 처리기가 그 되풀이를 끝까지
      따라가지 못하는 경우에도 **조용히 넘어가지 아니하고** 말한다(`E-TYPE-CYCLE-LIMIT`).

```lowent-거부: 정수와 부동은 저절로 섞이지 아니한다 · E-TYPE-MIX
module ex_mix .

fn f input a u32 . input b f64 . output f64 .
do
  return add a b .     rem 건너려면 그렇게 적힌 op 을 쓴다
end
```

## 8.10 링커가 주는 저장소 — `storage reserved`

(1) `storage reserved .` 블록은 **링커가 그 타입에 떼어 주는** 정적 저장소를 연다.
      프로그램이 실행 중에 얻는 것이 아니라, 지어질 때 이미 자리가 잡혀 있는 것이다.

(2) 이 블록을 여는 op 은 다음 셋을 갖추어야 한다.

> [!표] `storage reserved` 가 갖추어야 하는 것
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*갖출 것*], [*없으면*], [*왜*],
> [권한], [`E-RESERVE-NOCAP`], [링커가 건네는 자리도 **건네받는 것**이지 주워 쓰는 것이 아니다],
> [`effects state`], [`E-RESERVE-NOEFFECT`], [프로그램이 사는 동안 남는 자리이므로, 그것을 만지는 일은 상태를 만지는 일이다],
> [기계가 그것을 줄 수 있음], [`E-RESERVE-NOHOST`], [운영체제 위에서는 링커가 그 자리를 그렇게 주지 아니한다],
> )

## 8.11 괄호는 블록을 넘지 아니한다

(1) 열린 `(` 는 **같은 블록 안에서** 닫혀야 한다. 닫히지 않은 채 블록이 끝나면 거부되고
      (`E-PAREN-ESCAPE`), 아예 닫히지 않으면 거부된다(`E-PAREN-UNCLOSED`).

(2) 짝 없는 `)` 도, 블록의 경계를 넘어 짝을 찾는 `)` 도 거부된다(`E-PAREN-STRAY`).

> [!산문]
> 괄호가 블록을 넘어 짝을 찾을 수 있으면, 괄호 하나를 빠뜨린 소스가 **다른 뜻으로
> 온전히 읽히는** 일이 생긴다. 그때 처리기는 잘못을 말하는 대신 다른 프로그램을 짓는다.
> 경계를 못 넘게 하면 빠뜨림은 언제나 빠뜨림으로 드러난다.

```lowent-거부: 블록이 끝나도록 괄호가 안 닫혔다 · E-PAREN-ESCAPE
module ex_paren .

fn f output u8 .
do
  return (add 1 2 .
end
```

## 8.12 배타 — 읽는 이 여럿 **또는** 쓰는 이 하나

(1) 어떤 값에 닿는 길은 한 시점에 **읽기 여럿**이거나 **쓰기 하나**이며, 그 둘이 겹치는
      것은 적합하지 아니하다(`E-EXCL`). 겹치면 읽는 쪽이 보는 값이 **언제 바뀌는지**
      아무도 말할 수 없다.

(2) 빌린 것은 빌려준 값보다 **오래 살 수 없다.** 빌려준 값이 옮겨졌는데 빌림이 남아
      있으면 적합하지 아니하다(`E-EXCL-MOVED`).

(3) 빌림은 그것을 연 블록 **밖으로 나갈 수 없다**(`E-BORROW-ESCAPE`).

(4) 액터에게서 빌린 것을 들고 그 액터에게 다시 말을 거는 것은 적합하지 아니하다
      (`E-BORROW-EXCL`) — 그 액터가 자기 상태를 고치는 동안 우리는 그 상태를 보고 있다.

(5) `let` 으로 묶은 이름은 **고칠 수 없다**(`E-IMMUTABLE`). 고칠 이름은 `var` 로 묶는다.

> [!산문]
> 이 규칙이 지키는 것은 ⟦§8.8⟧ 과 같다 — **읽는 쪽의 믿음**이다. 다만 §8.8 이 「쓸 수
> 있는가」를 타입으로 가른다면, 여기는 「지금 이 자리에 누가 함께 있는가」를 가린다.
> 타입이 맞아도 함께 있으면 안 되는 자리가 있고, 그것을 보는 것이 배타 규칙이다.

> [!주의]
> `mut ref slice` 는 쓰지 아니한다(`E-MREF-SLICE`) — `mut slice` 가 이미 그 원소를 고칠 수
> 있게 하므로, 앞의 것은 **뒤의 것이 주지 않는 것을 하나도 주지 못한다.** 같은 것을
> 말하는 두 번째 철자다.

```lowent-거부: `let` 으로 묶은 이름은 고칠 수 없다 · E-IMMUTABLE
module ex_immutable .

fn f output u8 .
do
  let a u8 be 1 .
  set a 2 .            rem 고치려면 `var` 로 묶어야 한다
  return a .
end
```

## 8.13 얼로케이터 — 권한 · 정책 · 상태

> [!용어] 얼로케이터 (allocator)
> 뿌리에서 받은 바이트나 빌린 바이트를 **작은 자리로 나눠 주는** 값. 이 언어에서는 트레이트
> `byte_allocator` 를 갖춘 actor 다.

(1) 뿌리(⟦§8.1⟧ (2a)) 위에서 자리를 나눠 주는 것을 ⟦얼로케이터|allocator⟧ 라 한다. 얼로케이터는
      언어의 특별한 장치가 아니라 **트레이트 `byte_allocator` 를 갖춘 actor** 다. 그 말에는 세 가지가 있다.

> [!표] 얼로케이터를 이루는 셋
> #table(columns: (auto, auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*층*], [*언제 있나*], [*무엇인가*],
> [권한], [번역할 때만], [뿌리에 닿아도 되는가 — `cap allocator` · `cap heap`(⟦§7.2⟧)],
> [정책], [번역할 때(타입)], [어떤 규칙으로 깎는가 — `byte_allocator` 를 갖춘 **타입**],
> [상태], [실행 중], [커서·받침 바이트 — 그 타입의 **값**],
> )

(2) actor 의 상태에 권한 칸(`root cap heap .` · `root cap allocator .`)을 둘 수 있다. 그 칸으로
      `alloc_bytes` 를 부르면 그 권한의 뿌리에서 깎는다. 권한은 실행 중 값이 아니므로 그 칸은 크기가 없다.

(3) 권한 칸을 가진 actor 는 **같은 종류의 권한이 보이는 op** — 그 권한을 입력으로 받았거나, 스스로 같은
      종류의 칸을 가진 actor 의 op — 에서만 띄울 수 있다(`E-CAP-FORGE`). 권한 없는 곳에서 한 줄로 뿌리에
      닿는 길을 지어낼 수 없게 하기 위해서다.

(4) 운영체제가 없는 기계에서는 `cap heap` 칸을 가진 actor 와 `heap` 효과를 적은 actor 의 op 이 거부된다
      (`E-HEAP-NOHOST`) — 단, 그 단위 어디서도 띄우지 않는 actor 는 묻지 아니한다.

(5) 표준 라이브러리는 기본 얼로케이터 둘을 이 모양으로 낸다: `allocs.fixed_bytes`(고정 창) ·
      `allocs.heap_bytes`(힙). 그래서 기본 얼로케이터와 사용자가 지은 얼로케이터가 **같은 자리**에
      들어간다 — 받는 쪽(`input comptime a type . input al a . requires allocs.byte_allocator a .`)은 그 둘을
      가리지 아니한다.

(6) `heap` 효과는 «뿌리가 자란다» 만 뜻한다. 자리를 **낱낱이 돌려주는** 것은 정책의 일이며, 그런 얼로케이터는
      트레이트 `freeing_allocator` 를 갖춘다 — `byte_allocator` 의 op 에 더해 `release` 가 조각을 받아 돌려받았는지를
      참거짓으로 답한다. 그래서 운영체제가 없는 기계에서도 고정 창 위의 낱낱 반환 얼로케이터를 쓸 수 있다.

(7) 얼로케이터가 «내가 마지막에 준 그 조각인가» 를 물을 때는 **길이가 아니라 정체**로 묻는다: `grow` 와 `release` 는
      조각 자체를 받고, 내장 `same_slice a b` 로 두 슬라이스가 같은 자리에서 시작하고 길이가 같은지 본다. 길이만으로
      알아보면 같은 길이의 남의 조각을 늘리거나 돌려받는다. `same_slice` 는 주소를 밖에 내지 않는다 — 답은 참거짓이다.

```lowent-거부: 권한 없이 권한 칸을 가진 actor 를 띄운다 · E-CAP-FORGE
module ex_cap_forge .

actor grower do
  state do
    root cap heap .
  end .
  proc take input n u64 . output u64 . effects heap . do
    let g option mut slice u8 . . be alloc_bytes root capacity n .
    if is_some g . do return n . end .
    return 0 .
  end .
end .

proc f output u64 . effects heap state . do
  var g grower be spawn actor grower . .     rem `input h cap heap .` 가 없다
  return send g take 8 .
end .
```

## 8.14 객체마다 얼로케이터를 고르기 — `using`

(1) op 은 머리에 `using <이름> <타입> .` 절을 적어 **자기가 깎아 쓰는 얼로케이터**를 밝힐 수 있다.
      `<타입>` 은 `byte_allocator` 를 갖춘 타입이거나, 그런 경계(`requires allocs.byte_allocator a .`)를
      가진 comptime 타입 매개변수다. 본문에서 `<이름>` 은 평범한 이름이다.

(1a) 한 op 은 `using` 절을 **하나만** 적는다(`E-USING-DUP`). 절은 이름과 타입 두 낱말이며, 바인딩의 `using` 은
      `be` 바로 앞에 출처 이름 하나를 적는다 — 모양이 어긋나면 `E-USING-FORM` 이다.

(2) `using` 절은 **입력이 아니다.** 부르는 쪽은 그 얼로케이터를 위치로 적지 않으며, `<타입>` 이 타입
      매개변수이면 그 타입 인자도 적지 않는다 — 얼로케이터의 타입에서 온다.

(3) 부르는 쪽은 바인딩에서 얼로케이터를 고른다: `let <이름> <타입> using <출처> be <식> .` 고른 출처는 그
      초기식의 **머리 호출**에 들어간다. 머리 호출이 `using` 절을 갖지 않은 op 이면 적합하지 아니하다
      (`E-ALLOC-USING-UNUSED`) — 자기 얼로케이터를 이미 든 객체는 부르는 쪽의 선택을 받지 아니한다.

(4) 고르지 않으면 다음 차례로 기본값이 정해진다. 처음 맞는 것을 쓴다.

> [!표] 기본값을 정하는 차례
> #table(columns: (auto, 1fr), stroke: 0.5pt + rgb("#bbb"), inset: 6pt,
> [*차례*], [*출처*],
> [1], [바인딩의 `using <출처>`],
> [2], [이 op 자신의 `using` 절 이름(타입이 맞으면)],
> [3], [이 op 의 입력·바인딩 가운데 타입이 맞는 **하나뿐인** 것],
> [없음], [`E-ALLOC-NOSOURCE` — 보이는 얼로케이터가 없다],
> [둘 이상], [`E-ALLOC-AMBIGUOUS` — 짐작하지 않는다. 바인딩에 적어라],
> )

(5) 기본값은 **op 의 경계를 넘지 아니한다.** 출처는 그 op 의 서명과 본문에서만 오고, 부른 쪽의 얼로케이터가
      저절로 흘러들지 않는다. 필드도 출처로 세지 아니한다. 곧 **전역 얼로케이터는 없다.**

(5a) `using` 은 **나무 위에서** 풀린다 — 처리기가 arity 로 폼을 세운 뒤, 단형화 앞이다. 나무를 세우지 않는 대조
      방식으로 검사하면 풀리지 않은 절이 남으며, 처리기는 그것을 조용히 입력 하나 모자란 op 으로 검사하지 않고
      `E-USING-UNRESOLVED` 로 말한다.

(6) `alloc_bytes capacity <n>` 처럼 뿌리 피연산자를 생략할 수 있는 것은 **영역 블록 안**뿐이며, 그때 가장 안쪽
      영역이 그 자리에 든다. 권한(`cap allocator`·`cap heap`)은 생략하지 못하고 이름으로 적는다 — 이름 없이
      뿌리에 닿는 길을 만들지 않기 위해서다. 뿌리를 가리키지 않는 `alloc_bytes` — 영역 밖에서 생략했거나, 권한 입력 ·
      영역 매개변수 · 열린 영역 블록 · 권한 칸 가운데 어느 것도 아닌 이름을 댄 것 — 은 적합하지 아니하다(`E-ALLOC-NOROOT`).
      `stack_new` 의 뿌리는 영역 매개변수이며 생략하지 못한다(같은 코드).

(6a) 크기는 `capacity` 표식 뒤에 적는다: `alloc_bytes <뿌리> capacity <n>` · `stack_new <영역> capacity <n>`.
      표식이 없으면 적합하지 아니하다(`E-ALLOC-CAPACITY-MARK`) — 생략형과 이름을 댄 형을 가르는 것이 그 표식이다.

> [!산문]
> 얼로케이터는 값이다 — 커서와 받침 바이트는 실행 중에 어딘가 살아야 한다. `using` 은 그 값을 **숨기지 않고**
> 적는 자리를 옮긴 것이다: 받는 쪽은 서명에 «나는 이 얼로케이터에서 깎는다» 를 적고, 부르는 쪽은 객체를 만드는
> 줄에 «이것으로» 를 적는다. 둘 사이의 기계어는 인자 하나를 넘기는 것과 같다.

```lowent 예제: 한 op 에서 두 얼로케이터를 번갈아 쓴다
module ex_using .

trait carver do
  reserve input s self . input n u64 . output u64 . effects state .
end .

rem 정책 둘 — 하나는 요청만큼, 하나는 두 배씩 센다
actor exact do
  satisfies carver .
  state do
    used u64 .
  end .
  proc reserve input n u64 . output u64 . effects state . do
    set used (add used n) .
    return used .
  end .
end .

actor doubled do
  satisfies carver .
  state do
    used u64 .
  end .
  proc reserve input n u64 . output u64 . effects state . do
    set used (add used (mul n 2)) .
    return used .
  end .
end .

rem 받는 쪽 — 부르는 쪽은 얼로케이터도, 그 타입도 적지 않는다
proc take input comptime a type . using al a . input n u64 . output u64 . effects state . requires carver a . do
  return send al reserve n .
end .

proc main output u8 . effects state . do
  var e exact be spawn actor exact . .
  var d doubled be spawn actor doubled . .
  let x u64 using e be take 3 .      rem 3
  let y u64 using d be take 3 .      rem 6
  return narrow u8 (add x y) .
end .
```

```lowent-거부: 보이는 얼로케이터가 둘인데 고르지 않았다 · E-ALLOC-AMBIGUOUS
module ex_using_ambiguous .

trait carver do
  reserve input s self . input n u64 . output u64 . effects state .
end .

actor exact do
  satisfies carver .
  state do
    used u64 .
  end .
  proc reserve input n u64 . output u64 . effects state . do
    set used (add used n) .
    return used .
  end .
end .

proc take input comptime a type . using al a . input n u64 . output u64 . effects state . requires carver a . do
  return send al reserve n .
end .

proc main output u8 . effects state . do
  var e exact be spawn actor exact . .
  var f exact be spawn actor exact . .
  let x u64 be take 3 .              rem e 인가 f 인가 — 짐작하지 않는다
  return narrow u8 x .
end .
```
