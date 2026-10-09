#import "../lib.typ": *

#sub("6.10", "모듈 (Modules)")[
  #para("1")[
    소스 파일 하나가 #t("모듈", "module") 하나다. 모듈은 자기 이름을 첫 줄에 적는다.
  ]
  #para("2")[
    모듈 이름은 파일 이름과 같을 필요가 없다.
  ]
  #sub("6.10.1", "무엇을 내보내나")[
    #para("1")[
      `export` 를 붙인 것만 다른 모듈이 쓸 수 있다. 붙이지 않은 것은 그 모듈 안에서만 쓴다.
    ]
    #para("2")[
      곧 #strong[감춘 것이 기본];이다. 내보내는 쪽이 그것을 적어야 한다.
    ]
    #para("2a")[
      `export` 는 선언에 붙는다: `fn` · `proc` · `struct` · `enum` · `type` · `newtype` · `actor` ·
      `trait`. 모듈 상수(`let`) · 모듈 변수(`var`) · `test` 블록에는 붙지 아니한다(`E-TOPLEVEL`) ---
      상수는 모듈의 겉면이 아니고(op 으로 내준다), 시험은 그것을 가진 모듈의 것이다.
    ]
    #para("2b")[
      내보낸 op 의 서명이 내보내지 않은 이름난 타입(구조체·열거·newtype·액터)을 쓰면 처리기는
      그것을 알린다(`W-EXPORT-HIDDEN`) --- 들여온 쪽은 그 타입을 적을 수 없으므로 값을 받을
      이름을 지을 수 없고, 그 export 는 밖에서 쓸 수가 없다. 프로그램 자체는 적합하므로 거부가
      아니라 알림이다. 투명 별칭(`type`)은 바탕 타입을 적으면 되므로 여기 들지 아니한다.
      #strong[거부가 아닌 까닭];: 한 모듈만으로 이루어진 단위에서는 「밖」이 없어 그 export 를 쓸 수
      없는 이가 아무도 없다. 이 단위가 라이브러리로 쓰일지 프로그램으로 끝날지는 처리기가
      알 수 없으므로, 아직 해가 되지 않은 것을 거부하지 아니한다.
    ]
    #ex("내보내기", "module ex_export .

rem `export` 를 붙인 것만 다른 모듈이 쓴다.
export fn visible input n u32 . output u32 .
do
  return mul n. 2 . .
end .

rem 이것은 이 모듈 안에서만 쓴다.
fn hidden input n u32 . output u32 .
do
  return add n. 1 . .
end .")
    #plain[
      반대로 된 언어도 있다 — 기본이 공개이고 감추려면 표시하는 방식이다. 이 언어가
      감춤을 기본으로 두는 이유는, #strong[밖에서 보이는 것이 곧 약속];이기 때문이다. 실수로
      약속하는 일보다 실수로 감추는 일이 고치기 쉽다.
    ]
  ]
  #sub("6.10.2", "무엇을 가져오나")[
    #para("1")[
      `use` 는 다른 모듈을 가져온다. 가져올 것이 어디 있는지도 함께 적는다.
    ]
    #para("2")[
      가져온 모듈의 이름을 통해 그 안의 것을 부른다.
    ]
    #para("3")[
      가져올 자리에 그 모듈이 없으면 번역이 거부된다.
    ]
    #shape("가져오기 — 자리를 적는 갈래", "module <내 모듈 이름> .

use <가져올 모듈 이름> from \"<그 모듈의 자리>\" .")
    #para("4")[
      자리를 적지 아니하는 갈래도 있다. 이때 그 모듈은 #strong[같은 번역 단위 안에]; 있어야 한다 —
      여러 파일을 한 단위로 넘기면 그 안에서 찾는다.
    ]
    #shape("가져오기 — 같은 단위 안에서", "module <내 모듈 이름> .

use <가져올 모듈 이름> .")
    #para("4a")[
      `use <모듈> [from "<자리>"] as <별칭> .` 로 #strong[그 모듈을 부를 이름을 바꿀 수 있다.]; 별칭은
      모듈 이름이 서는 모든 자리에 선다 — op 을 한정하는 자리(`<별칭>.<op>`)도, 타입을 적는
      자리(`<별칭>.<액터>`)도 그렇다.
    ]
    #para("4b")[
      별칭을 적으면 #strong[원래 모듈 이름은 그 단위 안에서 서지 아니한다];(`E-USE-ALIASED`).
      별칭은 덧이름이 아니라 #strong[바꿔 부르기];다. 두 이름이 다 서면 같은 모듈을 두 철자로 부르는
      코드가 생기고, 읽는 사람이 둘이 같은 것인지 확인해야 한다 --- 별칭을 적은 까닭은 원래
      이름이 불편해서인데, 그 이름이 계속 서 있으면 별칭이 한 일이 없다.
    ]
    #plain[
      이 언어가 되풀이하는 규율 하나가 여기에도 선다: #strong[한 뜻에 한 철자.]; 같은 것을 적는 길이
      둘이면 읽는 사람이 둘 다 알아야 하고, 고치는 사람은 둘 다 고쳐야 한다. `use` 줄 자신은
      예외다 --- 거기가 바로 원래 이름을 적는 자리이기 때문이다.
    ]
    #shape("가져오기 — 이름을 바꿔서", "module <내 모듈 이름> .

use <가져올 모듈 이름> as <별칭> .")
    #para("5")[
      자리를 적지 아니한 이름이 그 단위 안에 없으면, 처리기는 #strong[그 사실을 말하여야 한다.];
      이 판의 처리기는 경고(`W-USE-EXTERNAL`)로 말한다 — 모듈을 찾아다니는 검색 경로가
      없으므로, 그 이름이 있는지 없는지를 이 단위만 보고는 알 수 없기 때문이다.
      그 이름을 실제로 쓰면 그때는 거부된다.
    ]
    #note[
      #strong[검색 경로가 없다는 것];이 이 설계의 요점이다. 이름만 적어 두고 도구가 어딘가에서
      찾아오게 하면, 그 프로그램이 무엇에 기대는지가 소스 밖의 지식이 된다 — 같은 소스가
      기계에 따라 다른 것을 가져올 수 있다. 자리를 적거나, 같은 단위로 함께 넘기거나,
      둘 중 하나다.
    ]
    #note[
      이 자리는 실제로 도는 예제 대신 #strong[틀];로 적었다. 가져오기는 그 파일이 실제로 있어야
      번역되므로, 어느 자리에서나 도는 예제를 만들 수 없다. 검사기가 컴파일해 보는 것은
      `#ex` 뿐이며, 틀은 그 대상이 아니다.
    ]
    #note[
      어디서 가져오는지를 소스에 적는 것은 이 언어의 규율과 이어진다 — 이름만 적고 어디서
      오는지는 도구가 알아서 찾는 방식이면, 그 프로그램이 무엇에 기대는지가 소스 밖의
      지식이 된다.
    ]
  ]
  #sub("6.10.3", "이름이 부딪히면")[
    #para("1")[
      한 모듈 안에서 같은 이름을 두 번 선언할 수 없다. 그렇게 하면 번역이 거부된다.
    ]
    #para("1a")[
      모듈의 최상위 이름은 #strong[한 이름 공간];에 든다 — 모듈 · `fn`·`proc`(`extern` 포함) · `type` · `newtype` ·
      `struct` · `enum` 과 그 갈래 · `actor` · `trait` · 모듈 `let`/`var` · `test`. 종류가 달라도 같은 이름이면
      거부된다(`E-NAME-DUP`). 칸 · 타입에 딸린 op(`<타입>.<이름>`) · actor 의 상태와 처리기 · trait 가 요구하는 op 은
      그 그릇 안에 들며, 그릇이 다르면 같은 이름을 쓸 수 있다.
    ]
    #para("2")[
      가져온 이름들이 서로 부딪히면, 부르는 자리에서 어느 것인지 밝혀 적어야 한다.
    ]
    #rejected("같은 이름을 두 번 선언", "module ex_dup .

fn f output u32 . do return 1 . end .
fn f output u32 . do return 2 . end .", "E-NAME-DUP")
    #rejected("종류가 달라도 한 이름 공간이다 — 모듈 let 과 fn", "module ex_dup_kinds .

let limit u64 3 .
fn limit output u64 . do return 1 . end .", "E-NAME-DUP")
  ]
  #sub("6.10.4", "선언의 차례")[
    #para("1")[
      모듈 안의 최상위 선언은 #strong[차례와 무관하다.]; 뒤에 선언한 op 을 앞에서 부를 수 있고,
      서로 부르는 op 도 쓸 수 있다.
    ]
    #para("2")[
      op 본문 안에서는 그렇지 않다 — 지역 이름은 #strong[쓰기 전에 선언해야]; 한다.
    ]
    #plain[
      두 규칙이 다른 이유는 읽는 방식이 다르기 때문이다. 파일 전체는 훑어보며 읽지만,
      함수 본문은 위에서 아래로 읽는다. 본문에서 아직 안 나온 이름이 쓰이면 읽는 사람이
      되돌아가야 한다.
    ]
  ]
  #sub("6.10.5", "op 안의 op — 로컬 op")[
    #para("1")[
      op 본문의 #strong[바로 아래];(본문 `do … end` 의 문장 자리)에 `fn`·`proc` 선언을 둘 수 있다. 이것을
      #t("로컬 op", "local op") 이라 한다. `if`·반복·블록 안이나 다른 로컬 op 안에 두는 것, 본문 안에 타입·actor·
      trait·`test`·모듈을 선언하는 것은 적합하지 아니하다(`E-LOCAL-PLACE`).
    ]
    #para("2")[
      로컬 op 의 이름은 #strong[그 op 의 본문 안에서만]; 보인다. 그 안에서는 최상위 선언처럼 차례와 무관하다 —
      선언 앞에서 부를 수 있고, 같은 op 의 로컬 op 끼리 서로 부르고 되돌아 부를 수 있다(#cref("6.10.4") (1)).
      다른 op 은 그 이름을 보지 못한다. 서로 다른 op 이 같은 로컬 이름을 쓸 수 있다.
    ]
    #para("3")[
      로컬 op 은 바깥 op 의 매개변수·지역·region·빌림·능력을 #strong[보지 않는다.]; 그런 이름을 쓰는 것은 적합하지
      아니하다(`E-LOCAL-CAPTURE`). 필요한 값은 매개변수로 건넨다. 로컬 op 은 보통 op 과 같은 머리(절)와 몸을
      가지며, 효과·계약·오류·소유의 규칙도 같다. 로컬 `proc` 의 효과는 그것을 부르는 자리의 효과다(#cref("7")).
    ]
    #para("4")[
      로컬 op 의 이름은 바깥 op 의 이름, 그 매개변수·지역, 모듈이 이미 가진 이름을 가리지 아니한다
      (`E-NAME-SHADOW`). 한 op 의 로컬 op 끼리 이름이 같을 수 없고(`E-NAME-DUP`), 기본 연산의 이름을 쓸 수
      없다(`E-NAME-BUILTIN`).
    ]
    #para("5")[
      로컬 op 은 `export`·`extern`·`unsafe` 를 달 수 없다(`E-LOCAL-EXPORT`) — 밖에서 쓰려면 최상위에 선언한다.
    ]
    #ex("쓰는 자리 가까이에 둔 보조 op", "module ex_local .

export fn count_selected input xs slice u8 . input mask u8 . output u64 .
do
  fn has_bits input x u8 . input bits u8 . output bool .
  do
    return eq bit_and x. bits. . bits. . .
  end .
  return pipe xs. do
    filter has_bits with mask. .
    count .
  end . .
end .")
    #rejected("로컬 op 은 바깥의 이름을 보지 않는다", "module ex_local_capture .

export fn f input n u64 . output u64 .
do
  fn g input k u64 . output u64 .
  do
    return add k. n. . .
  end .
  return g 1 . .
end .", "E-LOCAL-CAPTURE")
    #rejected("로컬 op 은 본문 바로 아래에만 둔다", "module ex_local_place .

export fn f input n u64 . output u64 .
do
  if gt n. 1 . do
    fn g input k u64 . output u64 .
    do
      return k. .
    end .
  end .
  return n. .
end .", "E-LOCAL-PLACE")
    #note[
      로컬 op 은 클로저가 아니다. 이름이 보이는 범위만 좁을 뿐, 바깥에서 무엇도 몰래 가져오지 않으므로
      처리기는 그것을 보통 op 처럼 다룬다 — 실행 중에 만들어지는 객체도, 숨은 인자도 없다.
    ]
  ]
]
