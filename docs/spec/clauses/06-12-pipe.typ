#import "../lib.typ": *

#sub("6.12", "한 줄기로 흘리기 — `pipe`")[
  #para("1")[
    `pipe` 는 원천(source)의 원소를 하나씩 꺼내 #t("스테이지", "stage") 를 차례로 지나게 하고,
      #t("종결자", "terminal") 하나로 끝내는 문(statement)이다. 모양은 다음과 같다.
  ]
  #shape("pipe 문", "pipe <원천> do
  <스테이지> .        rem 없어도 된다. 여럿일 수 있다.
  <종결자> .          rem 정확히 하나.
end")
  #plain[
    슬라이스를 훑는 반복은 거의 언제나 같은 뼈대다 — #emph[인덱스를 두고, 끝인지 보고, 원소를 꺼내 조건을 보고,
    무언가를 쌓고, 인덱스를 늘린다.]; 그 뼈대를 손으로 쓰면 #strong[하려는 일];(«숫자만 남기고 센다»)이 인덱스와 카운터
    사이에 묻히고, 경계를 한 칸 틀리는 실수가 거기서 난다. `pipe` 는 뼈대를 언어가 맡고, 사람은 #strong[하려는 일만];
    줄마다 한 낱말로 적게 한다. 그러면서도 손으로 쓴 반복과 똑같이 한 번만 훑고 중간 배열을 만들지 않는다(#cref("6.12.1")).
    아래 두 op 은 같은 답을 낸다.
  ]
  #ex("같은 일을 손으로 쓴 반복과 pipe 로 — 둘 다 한 번 훑는다", "module ex_pipe_why .

fn is_digit input c u8 . output bool . do return and (ge c 48) (le c 57) . end

rem 손으로 쓴 반복 — 카운터·인덱스·조건·증가를 사람이 하나하나 맞춘다.
fn digits_loop input s slice u8 . output u64 .
do
  var n be u64 0 .
  var i be u64 0 .
  while lt i (len s) . do
    if is_digit (idx s i) . do set n (add n 1) . end
    set i (add i 1) .
  end
  return n .
end

rem 같은 일을 pipe 로 — «숫자인 것만 남기고, 센다». 하는 일이 줄마다 한 낱말로 보인다.
fn digits_pipe input s slice u8 . output u64 .
do
  return pipe s do
    filter is_digit .
    count .
  end .
end")
  #para("2")[
    스테이지는 일곱이다: `filter` · `map` · `take` · `skip` · `scan` · `zip` · `enumerate`.
      종결자는 다섯이다: `collect into` · `fold` · `count` · `any` · `all`.
      이 목록 밖의 낱말을 스테이지 자리에 쓰는 것은 적합하지 아니하다(`E-PIPE-STAGE`).
  ]
  #para("2a")[
    `collect into <자리>` 의 그 자리는 흘러오는 값을 #strong[잃지 않고]; 담을 수 있어야 한다.
      더 넓은 값을 좁은 그릇에 담는 것은 적합하지 아니하다(`E-TYPE-COLLECT`) — 값을 잃는
      변환은 암묵적으로 일어나지 아니한다(#cref("6.2.5") (1)). 좁히려면 `map` 으로 #strong[적어서]; 한다.
  ]
  #para("2b")[
    각 낱말의 뜻은 다음과 같다. «흐름» 은 원천의 원소가 앞 스테이지를 지나 이 자리에 이르는 차례이고,
      «op» 은 이름 붙은 op 이다(4). 원소 타입을 `t`, 누산값 타입을 `a` 로 적는다.
      - `filter <op>` (스테이지, op 은 `t → bool`) — op 이 참인 원소만 다음으로 보낸다. 거짓인 원소는 버린다.
      - `map <op>` (스테이지, op 은 `t → u`) — 원소마다 op 을 불러 그 답을 다음으로 보낸다. 원소 타입이 `u` 로 바뀔 수 있다.
      - `take <n>` (스테이지) — 앞의 `n` 개만 보내고 #strong[훑기를 멈춘다];(#cref("6.12.2")). 원소가 `n` 보다 적으면 있는 만큼.
      - `skip <n>` (스테이지) — 앞의 `n` 개를 버리고 나머지를 보낸다.
      - `enumerate <op>` (스테이지, op 은 `(u64, t) → u`) — 순번 `i`(0 부터)와 원소를 op 에 주고 그 답을 보낸다.
      - `zip <원천2> <op>` (스테이지, op 은 `(t, t2) → u`) — 원소와 `원천2` 의 같은 자리 원소를 op 에 주고 그 답을 보낸다. #strong[짧은 쪽이 끝나면 흐름이 끝난다.];
      - `scan <초기값> <op>` (스테이지, op 은 `(a, t) → a`) — 누산값을 `초기값` 으로 두고, 원소마다 `누산값 = op(누산값, 원소)` 로 고친 #strong[뒤의]; 누산값을 보낸다. 초기값 자신은 보내지 아니한다.
      - `collect into <자리>` (종결자) — 흐름의 원소를 `<자리>`(`mut slice`)에 앞에서부터 담고, #strong[담은 개수];(`u64`)를 낸다. 자리는 흐름을 모두 담을 만큼 길어야 한다(2c).
      - `fold <초기값> <op>` (종결자, op 은 `(a, t) → a`) — `scan` 처럼 누산하되 마지막 누산값 하나(`a`)를 낸다. 흐름이 비면 초기값이다.
      - `count` (종결자) — 흐름의 원소 수(`u64`)를 낸다.
      - `any <op>` (종결자, op 은 `t → bool`) — op 이 참인 원소가 있으면 참. 흐름이 비면 거짓. 첫 참에서 멈춘다.
      - `all <op>` (종결자, op 은 `t → bool`) — 모든 원소에서 op 이 참이면 참. 흐름이 비면 참. 첫 거짓에서 멈춘다.
  ]
  #para("2c")[
    받는 자리가 차면 #strong[말없이 멈추지 아니한다.]; 흐름이 받는 자리보다 긴 것을 번역 시점에 알면 — 두 길이가
      머리의 계약(`requires eq (len x) N`, `array N T` 입력이 그리 된다)에 적혀 있고 사이의 스테이지가 개수를 모르는
      것(`filter`·`zip`)이 아니면 — 번역이 거부한다(`E-COLLECT-FULL`). 알 수 없으면 실행 중, 담지 못할 원소가 오는
      순간 받는 자리가 찼다고 말하며 멈춘다(`E-VM-BOUNDS`, 네이티브는 같은 문장의 panic). 들어갈 만큼만 담으려면
      `take` 로 #strong[적는다.];
  ]
  #para("2d")[
    `filter`·`any`·`all` 의 op 은 #strong[판정];이다 — 출력이 `bool` 이어야 한다. 수를 내는 op 을 주는 것은
      적합하지 아니하다(`E-PIPE-PRED`). 0 이 아닌 수를 참으로 읽는 규칙은 이 언어에 없다(#cref("6.2.16") (4)). #cref("6.12.3") 의 기본 연산
      `filter` 의 op 도 같다.
  ]
  #rejected("다섯을 셋 칸에 담는다 — 두 길이를 번역 시점에 안다", "module ex_collect_full .

fn dbl input a u8 . output u8 . do return wrap_add a a . end

proc over input xs array u8 5 . input out mut array u8 3 . output u64 . effects none . do
  return pipe xs do
    map dbl .
    collect into out .
  end .
end", "E-COLLECT-FULL")
  #rejected("수를 내는 op 을 판정 자리에 준다", "module ex_pipe_pred .

fn as_flag input a u8 . output u8 . do return a . end

fn nonzero input xs slice u8 . output u64 . do
  return pipe xs do
    filter as_flag .
    count .
  end .
end", "E-PIPE-PRED")
  #para("3")[
    한 `pipe` 문은 종결자를 #strong[정확히 하나]; 갖는다. 종결자 없이 끝나거나 둘을 두는 것은
      적합하지 아니하다.
  ]
  #para("4")[
    스테이지의 인자는 #strong[이름 붙은 op 을 가리키는 이름];이다. 이름 없는 함수(람다)는 이 언어에
      없으므로, 스테이지 인자로 그 자리에서 만든 함수를 줄 수 없다.
  ]
  #para("5")[
    스테이지 op 은 원소 하나만 받는다 — 매개변수가 정확히 하나이고 능력(capability)을 받지
      아니한다. 그렇지 않은 op 을 스테이지로 쓰는 것은 적합하지 아니하다(`E-FOLD-OP`). 문맥(7)을
      적으면 매개변수가 하나 더 있다.
  ]
  #para("5a")[
    `fold` 와 `scan` 의 op 은 매개변수가 둘이며, #strong[첫째가 누산값이고 둘째가 원소];다.
      한 걸음은 `누산값 = op(누산값, 원소)` 이므로 첫 매개변수의 타입과 출력 타입은 같아야
      한다. 다른 타입으로 적는 것은 적합하지 아니하다(`E-FOLD-ORDER`).
  ]
  #para("6")[
    `take` 와 `skip` 의 개수는 번역 시점에 정해진 음이 아닌 값이어야 한다(#cref("6.8")).
      실행 시점에야 알 수 있는 값을 주는 것은 적합하지 아니하다.
  ]
  #para("7")[
    op 을 부르는 스테이지와 종결자 — `filter`·`map`·`any`·`all`·`scan`·`fold`·`zip`·`enumerate` — 는
      op 이름 뒤에 #t("문맥", "context") `with <식>` 을 둘 수 있다. 문맥은 op 의 #strong[마지막 인자];로 건너간다:
      `filter`·`map`·`any`·`all` 은 (원소, 문맥), `fold`·`scan` 은 (누산값, 원소, 문맥), `zip` 은 (원소,
      짝 원소, 문맥), `enumerate` 는 (번호, 원소, 문맥)을 받는다. 매개변수 수나 문맥의 타입이 맞지 않거나
      문맥 매개변수가 `mut`·`owned` 이면 적합하지 아니하다(`E-PIPE-CONTEXT-ARG`). op 을 부르지 않는
      `take`·`skip`·`count`·`collect` 에 문맥을 적거나, op 이름 뒤에 `with` 아닌 낱말이 오거나, `with`
      뒤가 식 하나가 아니면 적합하지 아니하다(`E-PIPE-WITH`). `with` 는 이 자리의 표지일 뿐 낱말(부록 A.1)이
      아니다.
  ]
  #para("7a")[
    문맥은 #strong[참조 없는 복사값];이다. 문맥 매개변수의 타입은 — 구조체의 칸, 열거의 모든 갈래 알맹이
      (쓰이지 않는 갈래도), 별칭·newtype 의 밑 타입, 배열의 원소, option·result 의 알맹이까지 — 슬라이스,
      참조(`ref`·`mut_ref`), 포인터, 능력(`cap`), region, `owned` 값, actor 를 품지 아니하여야 한다
      (`E-PIPE-CONTEXT-TYPE`). 능력을 쓰는 식이 문맥을 #strong[만들]; 수는 있으나, 능력을 문맥 #strong[안에]; 넣어 건넬
      수는 없다.
  ]
  #para("7b")[
    문맥 식은 훑기 #strong[전에 한 번]; 평가되고, 그 값이 매 호출에 같은 값으로 건너간다. op 이 제 사본을
      고쳐도 다음 호출은 보관된 값을 받는다. 원천이 비어도 평가한다. 문맥이 있는 `pipe` 는 원천을 평가한 뒤
      절 차례대로 각 절의 피연산자(`zip` 의 짝, `scan`·`fold` 의 초깃값, `with` 의 식, `collect into` 의 받는
      자리)를 왼쪽부터 평가한다. 준비 중에 실패하면 뒤의 준비와 훑기는 일어나지 아니한다. 문맥 식의 효과는
      바깥 op 의 효과다(#cref("7")). 문맥이 없는 `pipe` 의 차례는 전과 같다 — 원천, 종결자의 피연산자, 스테이지
      차례.
  ]
  #ex("실행 중에 정해지는 문턱을 문맥으로", "module ex_with .

fn above input x u8 . input limit u8 . output bool .
do
  return gt x limit .
end

export fn count_above input xs slice u8 . input base u8 . output u64 .
do
  return pipe xs do
    filter above with wrap_add base 1 .
    count .
  end .
end")
  #rejected("문맥은 참조 없는 값이다", "module ex_with_slice .

fn over input x u8 . input s slice u8 . output bool .
do
  return gt x (idx s 0) .
end

export fn f input xs slice u8 . output u64 .
do
  return pipe xs do
    filter over with xs .
    count .
  end .
end", "E-PIPE-CONTEXT-TYPE")
  #rejected("op 을 부르지 않는 종결자는 문맥을 받지 않는다", "module ex_with_count .

fn above input x u8 . input limit u8 . output bool .
do
  return gt x limit .
end

export fn f input xs slice u8 . output u64 .
do
  return pipe xs do
    filter above with 3 .
    count with 1 .
  end .
end", "E-PIPE-WITH")
  #note[
    문맥은 람다가 아니다. 스테이지 op 은 여전히 이름 있는 op 이고, 바깥의 이름을 몰래 읽지 않는다 —
    문맥으로 건너간 값만 본다. 참조를 막는 까닭은 주소만 복사해 바깥 저장소를 훑기 내내 지켜보는 길을
    닫기 위해서다. 큰 표를 건네야 하면 일반 반복문이나 op 호출로 적는다.
  ]
  #sub("6.12.1", "융합은 최적화가 아니라 의미다")[
    #para("1")[
      한 `pipe` 문은 #strong[한 번의 훑기(one pass)]; 로 실행된다. 스테이지 사이에 중간 모음
      (intermediate collection)이 만들어지지 아니한다. 이것은 처리기가 시도해 볼 수 있는
      #t("최적화", "optimisation") 가 아니라 이 문의 #strong[정의];다.
    ]
    #para("2")[
      그러므로 처리기는 중간 모음을 만들지 아니하여야 한다. 만드는 처리기는 적합하지
      아니하다.
    ]
    #note[
      왜 정의로 두는가. 융합을 최적화로 두면 #emph["어디까지 융합되는가"]; 가 처리기마다 다르고,
      쓰는 사람은 #strong[절벽];을 만난다 — 한 줄을 고쳤더니 갑자기 중간 배열이 생기고 느려지는
      자리다. 그 절벽은 소스만 봐서는 안 보인다. 여기서는 융합 못 할 연산이 #strong[스테이지
      어휘에 아예 없어서];, 사고로 절벽에 떨어질 길이 없다.
    ]
  ]
  #sub("6.12.2", "필요한 만큼만 읽는다")[
    #para("1")[
      `any` 는 참을 내는 첫 원소에서, `all` 은 거짓을 내는 첫 원소에서 훑기를 멈춘다.
      멈춘 뒤의 원소는 읽히지 아니하며, 그 원소에 대한 스테이지도 실행되지 아니한다.
    ]
    #para("2")[
      `take n` 은 n 개를 지나보낸 뒤 훑기를 멈춘다.
    ]
    #note[
      이 성질이 있어야 끝이 없는 원천을 `pipe` 로 다룰 수 있다. 멈춤이 의미에 적혀 있지
      않으면, 끝없는 원천은 프로그램을 멈추지 않게 만든다.
    ]
    #ex("스테이지 둘과 종결자 하나 — 한 번의 훑기", "module ex_pipe .

fn over2 input a u8 . output bool . do return gt a 2 . end

fn dbl input a u8 . output u8 . do return (wrap_add a a) . end

rem [1,2,3,4,5] → 2 보다 큰 것만 → 두 배 → out 에 담는다. 중간 배열은 생기지 아니한다.
proc fm input xs slice u8 . input out mut slice u8 . output u64 . effects none . do
  pipe xs do
    filter over2 .
    map dbl .
    collect into out .
  end
  return 0 .
end
")
    #plain[
      `pipe` 가 문인 까닭은 융합을 #strong[약속으로]; 만들기 위해서다. 식이었다면 스테이지를 값으로
      떼어 넘길 수 있고, 그러면 어디까지가 한 줄기인지 소스에서 안 보인다. `do … end` 가 그
      경계를 눈에 보이게 못박는다.
    ]
  ]
  #sub("6.12.3", "`pipe` 밖의 `map`·`filter` — 받는 자리에 써 넣는 기본 연산")[
    #note[
      이 절의 `map`·`filter` 는 `pipe` 의 스테이지가 #strong[아니다.]; 이름이 같은 기본 연산이며, 문 하나로
      한 슬라이스를 다른 슬라이스에 옮겨 담는다: `map <받는 자리> <op> <원천> .` ·
      `filter <받는 자리> <op> <원천> .`. `pipe` 안의 스테이지는 op 하나만 받는다(#cref("6.12") (2b)).
    ]
    #para("1")[
      `map` 과 `filter` 는 결과를 #strong[첫 인자(받는 자리)에 써 넣는다.]; 그러므로 그 자리는
      고칠 수 있어야 한다(`E-MAP-SINK`, #cref("8.8")).
    ]
    #para("1a")[
      받는 자리가 차는 것은 #cref("6.12") (2c)와 같다 — 알면 번역이, 모르면 실행이 멈춘다. `map` 은 원천의 원소 수만큼,
      `filter` 는 판정을 지난 원소 수만큼 자리가 있어야 한다.
    ]
    #para("2")[
      이 두 연산이 다루는 원소는 #strong[스칼라];여야 한다(`E-MAP-ELEM`). 묶음과 열거는
      원소 하나를 한 번에 쓰는 이 길로는 다루지 아니한다.
    ]
    #plain[
      받는 자리를 인자로 받는 까닭은 #cref("9.2") 와 같다 — #strong[저장소를 부르는 쪽이 준다.]; 그러면
      이 스테이지는 무더기를 쓰지 않아도 되고, 얼마나 쓰는지가 부르는 자리에 보인다.
    ]
  ]
]
