#import "../lib.typ": *

= 할당기와 고정 메모리

#chapter-toc()

#prereq(
  ([#chref("regions") 영역], [고정 창과 힙, 두 뿌리]),
  ([#chref("capabilities") 권한], [권한은 종류로 인가되고 실행 비용이 없다]),
  ([#chref("effects") 효과], [`via` 는 타입 인자가 할당 효과를 정하게 한다]),
)

#deepqa[
  #chref("regions")에서 운영체제가 없는 기계를 대상으로 지으면 무엇이 거절되었고, 무엇은 여전히 쓸 수 있었는가?
][
  `heap` 효과·`cap heap` 입력·`region … heap` 블록이 모두 `E-HEAP-NOHOST` 로 거절되었다. 자라지 않는 고정 창에서 깎는
  `alloc` 은 여전히 쓸 수 있었다. 이 장은 그 고정 창 위에서, 그리고 남이 빌려준 바이트 위에서 자리를 나눠 주는
  *할당기*를 다룬다.
]

#why[
  영역은 "블록이 끝날 때 한꺼번에" 라는 한 가지 정책이다. 실제 프로그램은 더 많은 정책을 원한다. 커다란 버퍼 하나를
  받아 앞에서부터 잘라 쓰고 싶고, 정렬을 맞춰 자르고 싶고, 같은 코드를 힙에서도 고정 창에서도 돌리고 싶다. Lowent
  에서 할당기는 언어의 특별한 장치가 아니라 *트레이트를 갖춘 액터*다. 그래서 사용자가 만든 할당기와 표준 할당기가 같은
  자리에 들어간다. 메모리를 다루는 제5부의 마지막 장으로, 운영체제 없는 기계까지 이어지는 고정 메모리의 길을 정리한다.
]

#organizer[
  할당기가 권한·정책·상태 셋으로 이루어진다는 것을 알게 된다. 빌린 바이트를 잘라 주는 범프 할당기를 쓰고, 메모리
  부족이 값이라는 것을 확인한다. 할당기를 타입 매개변수로 받고 `using` 으로 건네는 법, 뿌리에서 곧장 깎는 기본 할당기와
  권한 칸의 규칙을 익힌다. 운영체제 없는 기계의 고정 창 크기를 링커가 정하는 방식과, 비트를 그대로 둔 채 읽는 법만 바꾸는
  `bit_cast` 도 보게 된다.
]

#chapter-questions()

#idx("할당기")
== 할당기를 이루는 셋

#dtable(
  columns: 3,
  id: "fixed-layers",
  caption: [할당기를 이루는 셋],
  [*층*], [*언제 있나*], [*무엇인가*],
  [권한], [번역할 때만], [뿌리에 닿아도 되는가 --- `cap allocator` · `cap heap`],
  [정책], [번역할 때(타입)], [어떤 규칙으로 깎는가 --- `byte_allocator` 트레이트를 갖춘 *타입*],
  [상태], [실행 중], [커서와 받침 바이트 --- 그 타입의 *값*(액터)],
)

표준 라이브러리 `allocs` 의 트레이트 `byte_allocator` 는 세 op 을 요구한다. `reserve n` 은 `n` 바이트를 잘라 `option`
으로 주고, `grow` 는 마지막 조각을 제자리에서 늘리며, `used` 는 지금까지 쓴 양을 답한다. 액터(#chref("actors"))는
자기 상태를 들고 `send` 로만 말을 거는 객체다. 여기서는 `spawn actor` 로 만들고 `send a reserve 3` 으로 부른다는 것만
알면 된다.

== 빌린 바이트를 잘라 준다

가장 단순한 할당기는 *범프*다. 커서 하나를 두고, 달라는 만큼 잘라 준 뒤 커서를 민다.

```text
    buf (8 바이트)
    [ a  a  a │ b  b │ ·  ·  · ]
      └ reserve 3 이 준 조각
                └ reserve 2 가 준 조각
                       ▲ 커서 (used = 5) --- 다음 reserve 는 여기서부터 자른다

reserve 4 를 청하면 남은 것은 3 바이트뿐 → none (모자람도 값이다)
```

#demo("examples/ch20/borrowed.low")

- `spawn actor allocs.bump_bytes` 로 할당기를 만들고, `send a init buf` 로 잘라 줄 원본 바이트를 건다. 이 할당기는
  스스로 메모리를 만들지 못한다. 몰래 할당하지 않는 규율이다.
- `send a reserve 3` 은 3 바이트를 잘라 준다. 돌려받은 것은 복사본이 아니라 원본의 일부를 가리키는 슬라이스라서,
  `set (index pv 0) 65 .` 가 호출자의 `buf` 첫 바이트를 바꾼다. VM 이 보여 주는 인자 `[65,0,…]` 가 그 흔적이다.
- `send a reserve 99` 는 자리가 모자라 `none` 을 준다. 트랩이 아니다. *메모리 부족은 값*이고, 부르는 쪽이 검사한다.

이 op 의 머리에는 `cap allocator` 도 `alloc` 도 없다. 효과는 `state` 뿐이다. 바이트는 호출자가 빌려주었고, 할당기는
그것을 나눠 줄 뿐이기 때문이다. 권한이 능력을 가른다 --- 할당 권한을 받지 않은 코드도 남이 준 바이트 위에서는 온전히
할당한다.

#qa[
  범프 할당기로 조각 하나를 돌려줄 수 있는가?
][
  마지막 조각 하나만 돌려받는다(`release`, 트레이트 `freeing_allocator`). 그 밖의 조각은 `false` 를 답하고 아무것도
  바꾸지 않는다. 모르는 조각을 받아 주는 척하면 두 번 돌려주기가 남의 자리를 지우기 때문이다. 받고 놓기를 되풀이하는
  모양이면 세대 핸들을 쓰는 `pool` 이 맞다(#chref("lib-alloc")).
]

== 할당기를 갈아 끼운다

할당기를 쓰는 코드는 어느 구현인지 몰라도 된다. 할당기의 *타입*을 번역 시점 매개변수로 받고, 그 *값*은 `using` 절로
받는다.

#demo("examples/ch20/generic.low")

- `input comptime a type .` 은 할당기의 타입이다. 번역할 때 구체 타입으로 정해진다(#chref("generics")).
#idx("using")
- `using al a .` 은 그 타입의 할당기 값을 `al` 이라는 이름으로 받는다. `using` 은 입력이 아니다. 부르는 쪽은 인자
  자리에 적지 않고, 바인딩에 `let n u64 using b be two_from .` 으로 적는다.
- `requires allocs.byte_allocator a .` 는 `a` 가 트레이트를 갖추어야 한다는 조건이다(#chref("traits")).
- `effects state via a .` 는 할당기의 `reserve` 가 내는 효과가 곧 이 op 의 효과라는 뜻이다.

같은 `two_from` 에 `bump_bytes` 를 주면 3 + 5 = 8 을 쓰고, 8 의 배수로 시작점을 맞추는 `bump_aligned` 를 주면 둘째
조각이 8 에서 시작해 13 을 쓴다. 번역할 때 타입이 확정되므로 가상 함수 표도 간접 호출도 없다. 갈아 끼우기의 실행
비용이 0 이다.

출처를 적지 않으면 기본값이 정해진다. 바인딩의 `using`, 그 op 의 `using` 이름, 그리고 타입이 맞는 입력·바인딩이
*하나뿐*이면 그것을 쓴다. 둘 이상이면 짐작하지 않고 `E-ALLOC-AMBIGUOUS` 로 적으라고 한다. 기본값은 op 경계를 넘지
않는다. 부른 쪽의 할당기가 저절로 흘러드는 일이 없으니, *전역 할당기는 없다.*

#dtable(
  columns: 3,
  id: "fixed-using-lattice",
  caption: [할당기 출처를 정하는 차례 --- 위에서 처음 맞는 것],
  [*차례*], [*출처*], [*왜 이 자리인가*],
  [1], [바인딩에 적은 `using <이름>`], [적은 것이 언제나 이긴다],
  [2], [이 op 의 `using` 절 이름 가운데 타입이 맞는 *유일한* 것], [op 이 스스로 받겠다고 한 할당기],
  [3], [이 op 의 입력·바인딩 가운데 타입이 맞는 *유일한* 것], [눈에 보이는 값이 하나뿐이면 헷갈릴 일이 없다],
  [없음], [`E-ALLOC-NOSOURCE`], [전역 할당기로 메우지 않는다],
  [둘 이상], [`E-ALLOC-AMBIGUOUS`], [짐작하지 않고 후보 이름을 모두 말한다],
)

필드는 세지 않는다. 구조체 안에 숨은 할당기가 조용히 쓰이면, 어느 버퍼가 줄어드는지 코드를 읽어서 알 수 없기 때문이다.

출처가 하나도 없으면 이렇게 거절된다.

#demo("examples/ch20/nosource.low")

`caller` 에는 입력도, 띄운 액터도, `using` 절도 없다. 다른 언어라면 여기서 전역 힙이 조용히 쓰였을 것이다. Lowent 는 할당기를
입력으로 받거나, 이 자리에서 만들거나, `using` 절을 적으라고 한다. 호출이 거절되었으니 `caller` 의 `state` 도 실제로 쓰이지 않아
`W-EFFECT-OVER` 가 함께 붙는다. 첫 오류를 고치면 사라진다.

거꾸로, 할당기를 쓰지 않는 호출에 `using` 을 적어도 거절된다.

#demo("examples/ch20/usingunused.low")

`twice` 는 수를 두 배로 할 뿐 할당기를 받지 않는다. 쓰이지 않을 선택을 적어 두면 읽는 사람은 `twice` 가 메모리를 쓴다고
오해한다. 그래서 적은 것은 반드시 쓰여야 한다.

== 뿌리에서 곧장 깎는 기본 할당기

`allocs` 는 뿌리에서 곧장 깎는 할당기 둘도 같은 트레이트로 낸다.

#dtable(
  columns: 3,
  id: "fixed-defaults",
  caption: [기본 할당기],
  [*액터*], [*상태*], [*`reserve` 의 효과*],
  [`fixed_bytes`], [`root cap allocator .` · 쓴 양], [`alloc state` --- 어디서나],
  [`heap_bytes`], [`root cap heap .` · 쓴 양], [`heap state` --- 운영체제가 있는 기계만],
)

#demo("examples/ch20/fixed.low")

`fixed_bytes` 의 상태에는 *권한 칸*이 있다. 권한은 실행 중 값이 아니므로 그 칸은 크기가 없다. 대신 규칙이 하나 붙는다.
권한 칸을 가진 액터는 *같은 종류의 권한을 쥔 op* 에서만 띄울 수 있다.

#demo("examples/ch20/forge.low")

`sneaky` 는 `cap heap` 을 받지 않았는데 `heap_bytes` 를 띄우려 했다. 이것이 허락되면 권한 없는 곳에서 한 줄로 힙을
지어낼 수 있다. 권한은 건네받는 것이지 주워 쓰는 것이 아니다.

권한을 받으면 같은 일이 된다.

#demo("examples/ch20/heapbytes.low")

`main` 이 `cap heap` 을 받았고 효과 줄에 `heap` 을 적었다. 그래서 `heap_bytes` 를 띄울 수 있고, 고정 창보다 큰 100000
바이트도 받는다. 힙은 모자라면 청크를 더 잇는다. 운영체제가 없는 기계를 대상으로 지으면 이 파일은 `E-HEAP-NOHOST` 로
거절된다(#chref("regions")).

== 조각을 늘리고 돌려준다 --- `grow` 와 `release`

범프 할당기는 앞으로만 민다. 그래도 *마지막에 준 조각*만은 되돌려도 안전하다. 그 뒤로 아무도 자리를 받지 않았기 때문이다.
`grow` 는 그 조각을 제자리에서 늘리고, `release`(트레이트 `freeing_allocator`)는 그 조각을 돌려받는다.

#demo("examples/ch20/growrelease.low")

#idx("same_slice")
- `send b grow pv 6` 은 4 바이트였던 `pv` 를 6 바이트로 늘린다. 늘리는 대상은 크기가 아니라 *조각 자체*다. 구현은 내장
  `same_slice a b`(시작 주소와 길이가 같은가)로 `pv` 가 방금 준 바로 그 바이트인지 확인한다. 길이만 같은 남의 버퍼를 넘기면
  `none` 이다. 크기만으로 알아보면 두 그릇이 조용히 겹치기 때문이다.
- `qv` 를 받은 뒤에는 `gv` 가 더 이상 마지막이 아니다. 그래서 `release gv` 는 `false` 이고 아무것도 바꾸지 않는다.
- `release qv` 는 `true` 다. 커서가 6 으로 돌아가므로 `used` 가 6 이다. 답 601 은 "쓴 양 6 · 첫 답 거짓 · 둘째 답 참" 이다.

두 op 모두 *최적화이지 약속이 아니다*. 못 늘리면 부르는 쪽이 새로 받아 복사하면 되고, 답은 같아야 한다. 뿌리에서 곧장 깎는
`fixed_bytes`·`heap_bytes` 의 `grow` 는 언제나 `none` 이다. 뿌리는 마지막 조각이 누구 것인지 모른다.

== 표준 라이브러리에서 셋은 어디에 있나

#dtable(
  columns: 4,
  id: "fixed-axes",
  caption: [할당에 쓰는 모듈 --- 권한 · 정책 · 상태 가운데 무엇을 맡나],
  [*모듈 · 이름*], [*권한*], [*정책(타입)*], [*상태(값)*],
  [`allocs.bump_bytes` · `bump_aligned`], [없다 --- 빌린 바이트], [`byte_allocator` · `freeing_allocator` 를 갖춘 액터], [받침 바이트 · 커서],
  [`allocs.fixed_bytes` · `heap_bytes`], [권한 칸 `cap allocator` · `cap heap`], [`byte_allocator`], [쓴 양],
  [`vecgen.vec t a`], [없다], [할당기 타입 `a` 를 매개변수로 받는다], [원소 수 · 버퍼 --- 할당기는 `open` 이 `using` 으로 받는다],
  [`growvec.gvec`], [없다], [`vecgen.vec u8 allocs.bump_bytes` 로 고정한 이름], [`vecgen` 과 같다],
  [`pool.block_pool b`], [없다 --- 빌린 바이트], [할당기가 아니다 --- 세대 핸들로 블록을 낱낱이 돌려받는 구조체], [블록 · 세대 배열],
)

권한은 뿌리에 닿는 두 액터에만 있다. 나머지는 모두 누군가 건네준 바이트 위에서 일한다. 그래서 권한을 받지 않은 라이브러리
코드도 그릇을 만들고 키울 수 있고, 어느 코드가 새 메모리를 프로그램에 들여오는지는 권한을 받은 op 만 보면 안다.

== 고정 창의 크기는 누가 정하나

운영체제가 없는 기계에서 고정 창은 링커가 정한 두 기호 사이의 메모리다. 크기는 실행 파일에 박히지 않는다. 컴파일러가
링커 스크립트 조각을 내주고, 보드마다 그 조각의 크기만 바꾼다.

```text
$ lowentc --emit-ldscript --fixed-bytes 4096 fixed.low
.lw_fixed (NOLOAD) : ALIGN(8)
{
    __lw_fixed_start = .;
    . = . + 4096;
    __lw_fixed_end = .;
}
```

호스트에서 다른 보드를 흉내 낼 때는 VM 에 창의 크기를 준다. `fixed.low` 는 64 바이트를 두 번 청하므로,
`lowentc --fixed-bytes 100 --run main fixed.low` 로 창을 100 바이트로 줄이면 둘째 `reserve` 가 `none` 이 되어 `main() = 2` 가
나온다. 작은 기계에서 어디서 메모리가 모자라는지를 개발 기계에서 먼저 볼 수 있다.

#misconception[임베디드에서는 동적 할당을 쓰지 않으니 할당기가 필요 없다][
  "동적 할당 금지" 의 속뜻은 *자라는 힙*과 *실패를 숨기는 할당*을 쓰지 말라는 것이다. 부팅 때 한 번 정해진 창에서
  깎고, 부족을 값으로 돌려주고, 한꺼번에 되감는 할당은 그 규율과 충돌하지 않는다. Lowent 의 고정 창은 크기가 링커에
  적히고, 부족은 `none` 이며, 힙 효과는 번역에서 막힌다. 규칙을 문법이 지키는 것이다.
]

== 비트는 그대로, 읽는 법만

같은 바이트를 다른 타입으로 읽는 일은 두 갈래다. 바이트 슬라이스 위에 구조체 배치를 얹는 `view` 는
#idx("bit_cast")
#chref("named-types")에서 보았다. 폭이 같은 두 스칼라 사이에서 비트열을 그대로 두고 읽는 법만 바꾸는 것은 `bit_cast`
다.

#demo("examples/ch20/bitcast.low")

`i32` 의 −1 은 모든 비트가 1 이므로 `u32` 로 읽으면 4294967295 다. `f64` 의 1.0 을 `u64` 로 읽으면 IEEE 754 표현이
그대로 나온다. `cast` 와 달리 값을 옮기지 않으므로 멈추는 일도 없다.

목표 타입은 *모든 비트열이 쓸모 있는 값*이어야 한다. `bool` 과 `enum` 은 그렇지 않다.

#demo("examples/ch20/bitcast_bool.low")

`u8` 의 2 를 `bool` 로 읽으면 참도 거짓도 아닌 값이 된다. 그런 값이 만들어지는 길을 막는다.

== 흔한 실수

#antipattern[범프 할당기에 `init` 을 잊는다][
  #demo("examples/ch20/mistake_noinit.low")

  `bump_bytes` 는 스스로 메모리를 만들지 않는다. 잘라 줄 바이트를 `send a init buf` 로 걸기 전에는 나눠 줄 것이 없다. 갓 띄운 액터의
  상태 칸은 모두 0 이고, 0 은 슬라이스가 아니다 --- 그 자리를 읽으면 VM 과 네이티브가 서로 다르게 돈다. 그래서 번역이 거절한다
  (`E-ACTOR-UNINIT`). 할당기를 띄우는 줄 바로 다음에 `init` 을 둔다.
]

#antipattern[같은 바이트를 두 할당기에 건다][
  #demo("examples/ch20/mistake_sharedbuf.low")

  두 할당기는 서로를 모른다. 둘 다 `buf` 의 앞에서부터 잘라 주므로 `pv` 와 `qv` 가 같은 자리가 되고, 65 를 쓴 뒤 66 을 쓰면 `pv` 를
  읽어도 66 이다. 쓰기 빌림은 하나여야 한다는 규칙(#chref("references"))이 액터 경계에서도 서야 하므로 `E-EXCL` 로 거절한다
  (2026-09-16 까지는 통과했다). 할당기마다 따로 된 바이트를 건다. 한 버퍼를 나눠야 하면 `subslice` 로 겹치지 않는 두 조각을 만든다.
]

#antipattern[맞는 할당기가 둘인데 `using` 을 적지 않는다][
  #demo("examples/ch20/mistake_ambiguous.low")

  `s` 와 `g` 가 모두 `bump_bytes` 라서 도구가 짐작할 수 없다. 짐작하면 작은 버퍼에서 깎아야 할 것을 큰 버퍼에서 깎거나 그 반대가 되고,
  그런 결함은 메모리가 넉넉한 개발 기계에서는 드러나지 않는다. 그래서 `E-ALLOC-AMBIGUOUS` 로 멈추고 적으라고 한다.

  #demo("examples/ch20/ambiguous_fixed.low")

  두 번 부른 `two_from` 이 같은 `g` 에서 3 바이트씩 깎았으므로 `used` 가 6 이다. 출처가 호출마다 적혀 있으니 어느 버퍼가 줄어드는지
  읽어서 안다.
]

#antipattern[제네릭 op 에서 `via a` 를 빠뜨린다][
  #demo("examples/ch20/mistake_novia.low")

  `one_from` 은 `effects state .` 만 적었지만, `fixed_bytes` 로 단형화하면 `reserve` 가 `alloc` 을 낸다. `via a` 가 있어야 "타입 `a` 가
  내는 할당 계열 효과도 내 선언이다" 가 되어 인스턴스마다 효과가 정확해진다. 없으면 `E-EFFECT` 가 나고, 효과가 부르는 쪽에 번지지 않아
  `with_fixed` 에는 엉뚱하게 `W-EFFECT-OVER` 까지 붙는다. 첫 오류를 고치면 둘째도 사라진다.
]

#misconception[할당기를 하나 더 띄우면 창도 하나 더 생긴다][
  #demo("examples/ch20/shared_window.low")

  `fixed_bytes` 는 *뿌리*에서 깎는다. 뿌리는 하나이고 커서도 하나다(#chref("regions")). `f1` 이 40000 바이트를 가져가면 `f2` 는 아직 아무것도
  쓰지 않았어도 기본 창(65536 바이트)에 남은 자리가 모자라 `none` 을 받는다. 할당기 값의 `used` 는 *그 할당기가* 쓴 양이지 창 전체의 남은
  양이 아니다. 따로 된 예산이 필요하면 창에서 한 번 크게 받아 `bump_bytes` 여럿에 겹치지 않게 나눠 건다.
]

== 이 장의 문법을 한눈에

#dtable(
  columns: 3,
  id: "fixed-memory-glance",
  caption: [할당기의 문법 --- 모양 · 뜻 · 왜 이렇게 생겼나],
  [*모양*], [*뜻*], [*왜 이렇게*],
  [`var a allocs.bump_bytes be spawn actor allocs.bump_bytes .`], [할당기(상태)를 띄운다], [상태는 액터 값 --- 전역 할당기가 없다],
  [`send a init buf`], [잘라 줄 바이트를 건다], [할당기는 몰래 메모리를 만들지 않는다],
  [`send a reserve 3` · `send a used`], [조각을 청한다(`option`) · 쓴 양], [부족은 트랩이 아니라 값],
  [`input comptime a type .`], [할당기의 타입(정책)을 번역 때 받는다], [갈아 끼우기의 실행 비용이 0],
  [`using al a .`], [그 타입의 할당기 값을 받는다 --- 입력이 아니다], [부르는 자리의 인자에 끼지 않는다],
  [`let n u64 using g be two_from .`], [이 호출이 깎을 할당기를 적는다], [둘 이상이면 짐작하지 않는다],
  [`effects state via a .` · `requires allocs.byte_allocator a .`], [타입의 효과를 물려받는다 · 트레이트 조건], [인스턴스마다 효과가 정확하다],
  [`allocs.fixed_bytes` · `allocs.heap_bytes`], [뿌리에서 곧장 깎는 기본 할당기], [같은 종류의 권한을 쥔 op 만 띄운다 --- `E-CAP-FORGE`],
  [`send b grow pv 6` · `send b release qv`], [마지막 조각을 늘린다 · 돌려받는다], [크기가 아니라 `same_slice` 로 정체를 확인한다],
  [출처 없음 · 쓰이지 않는 `using`], [`E-ALLOC-NOSOURCE` · `E-ALLOC-USING-UNUSED`], [전역 할당기도, 헛된 선택도 없다],
  [`bit_cast u32 x`], [비트는 그대로 두고 읽는 법만 바꾼다], [`bool`·`enum` 으로는 읽지 않는다],
)

#recap[
  할당기는 권한·정책(트레이트를 갖춘 타입)·상태(액터 값) 셋이다. 범프 할당기는 빌린 바이트를 잘라 주고 부족을 `none`
  으로 알린다. 할당기는 `input comptime a type .` 과 `using al a .` 로 받아 갈아 끼우며 실행 비용이 없고, 전역 할당기는
  없다. `fixed_bytes`·`heap_bytes` 는 권한 칸을 가진 기본 할당기이고, 같은 종류의 권한을 쥔 op 만 띄울 수 있다. 고정
  창의 크기는 링커가 정한다. `bit_cast` 는 비트를 그대로 두고, `bool`·`enum` 으로는 읽지 않는다.
]
