#import "../lib.typ": *

#sub("6.12", "한 줄기로 흘리기 — `pipe`")[
  #para("1")[
    `pipe` 는 원천(source)의 원소를 하나씩 꺼내 #t("스테이지", "stage") 를 차례로 지나게 하고,
      #t("종결자", "terminal") 하나로 끝내는 문(statement)이다. 모양은 다음과 같다.
  ]
  #shape("pipe 문", "pipe <원천> do
  <스테이지> .        rem 없어도 된다. 여럿일 수 있다.
  <종결자> .          rem 정확히 하나.
end .")
  #plain[
    슬라이스를 훑는 반복은 거의 언제나 같은 뼈대다 — *인덱스를 두고, 끝인지 보고, 원소를 꺼내 조건을 보고,
    무언가를 쌓고, 인덱스를 늘린다.* 그 뼈대를 손으로 쓰면 **하려는 일**(«숫자만 남기고 센다»)이 인덱스와 카운터
    사이에 묻히고, 경계를 한 칸 틀리는 실수가 거기서 난다. `pipe` 는 뼈대를 언어가 맡고, 사람은 **하려는 일만**
    줄마다 한 낱말로 적게 한다. 그러면서도 손으로 쓴 반복과 똑같이 한 번만 훑고 중간 배열을 만들지 않는다(#cref("6.12.1")).
    아래 두 op 은 같은 답을 낸다.
  ]
  #ex("같은 일을 손으로 쓴 반복과 pipe 로 — 둘 다 한 번 훑는다", "module ex_pipe_why .

fn is_digit input c u8 . output bool . do return and (ge c 48) (le c 57) . end .

rem 손으로 쓴 반복 — 카운터·인덱스·조건·증가를 사람이 하나하나 맞춘다.
fn digits_loop input s slice u8 . output u64 .
do
  var n u64 be 0 .
  var i u64 be 0 .
  while lt i (len s) . do
    if is_digit (index s i) . do set n (add n 1) . end .
    set i (add i 1) .
  end .
  return n .
end .

rem 같은 일을 pipe 로 — «숫자인 것만 남기고, 센다». 하는 일이 줄마다 한 낱말로 보인다.
fn digits_pipe input s slice u8 . output u64 .
do
  return pipe s do
    filter is_digit .
    count .
  end .
end .")
  #para("2")[
    스테이지는 일곱이다: `filter` · `map` · `take` · `skip` · `scan` · `zip` · `enumerate`.
      종결자는 다섯이다: `collect into` · `fold` · `count` · `any` · `all`.
      이 목록 밖의 낱말을 스테이지 자리에 쓰는 것은 적합하지 아니하다(`E-PIPE-STAGE`).
  ]
  #para("2a")[
    `collect into <자리>` 의 그 자리는 흘러오는 값을 **잃지 않고** 담을 수 있어야 한다.
      더 넓은 값을 좁은 그릇에 담는 것은 적합하지 아니하다(`E-TYPE-COLLECT`) — 값을 잃는
      변환은 암묵적으로 일어나지 아니한다(#cref("6.2.5") (1)). 좁히려면 `map` 으로 **적어서** 한다.
  ]
  #para("3")[
    한 `pipe` 문은 종결자를 **정확히 하나** 갖는다. 종결자 없이 끝나거나 둘을 두는 것은
      적합하지 아니하다.
  ]
  #para("4")[
    스테이지의 인자는 **이름 붙은 op 을 가리키는 이름**이다. 이름 없는 함수(람다)는 이 언어에
      없으므로, 스테이지 인자로 그 자리에서 만든 함수를 줄 수 없다.
  ]
  #para("5")[
    스테이지 op 은 원소 하나만 받는다 — 매개변수가 정확히 하나이고 능력(capability)을 받지
      아니한다. 그렇지 않은 op 을 스테이지로 쓰는 것은 적합하지 아니하다(`E-FOLD-OP`).
  ]
  #para("6")[
    `take` 와 `skip` 의 개수는 번역 시점에 정해진 음이 아닌 값이어야 한다(#cref("6.8")).
      실행 시점에야 알 수 있는 값을 주는 것은 적합하지 아니하다.
  ]
  #sub("6.12.1", "융합은 최적화가 아니라 의미다")[
    #para("1")[
      한 `pipe` 문은 **한 번의 훑기(one pass)** 로 실행된다. 스테이지 사이에 중간 모음
      (intermediate collection)이 만들어지지 아니한다. 이것은 처리기가 시도해 볼 수 있는
      #t("최적화", "optimisation") 가 아니라 이 문의 **정의**다.
    ]
    #para("2")[
      그러므로 처리기는 중간 모음을 만들지 아니하여야 한다. 만드는 처리기는 적합하지
      아니하다.
    ]
    #note[
      왜 정의로 두는가. 융합을 최적화로 두면 *"어디까지 융합되는가"* 가 처리기마다 다르고,
      쓰는 사람은 **절벽**을 만난다 — 한 줄을 고쳤더니 갑자기 중간 배열이 생기고 느려지는
      자리다. 그 절벽은 소스만 봐서는 안 보인다. 여기서는 융합 못 할 연산이 **스테이지
      어휘에 아예 없어서**, 사고로 절벽에 떨어질 길이 없다.
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
  end .
  return 0 .
end
")
    #plain[
      `pipe` 가 문인 까닭은 융합을 **약속으로** 만들기 위해서다. 식이었다면 스테이지를 값으로
      떼어 넘길 수 있고, 그러면 어디까지가 한 줄기인지 소스에서 안 보인다. `do … end` 가 그
      경계를 눈에 보이게 못박는다.
    ]
  ]
  #sub("6.12.3", "원소를 옮겨 담는 스테이지")[
    #para("1")[
      `map` 과 `filter` 는 결과를 **첫 인자(받는 자리)에 써 넣는다.** 그러므로 그 자리는
      고칠 수 있어야 한다(`E-MAP-SINK`, #cref("8.8")).
    ]
    #para("2")[
      이 두 스테이지가 다루는 원소는 **스칼라**여야 한다(`E-MAP-ELEM`). 묶음과 열거는
      원소 하나를 한 번에 쓰는 이 길로는 다루지 아니한다.
    ]
    #plain[
      받는 자리를 인자로 받는 까닭은 #cref("9.2") 와 같다 — **저장소를 부르는 쪽이 준다.** 그러면
      이 스테이지는 무더기를 쓰지 않아도 되고, 얼마나 쓰는지가 부르는 자리에 보인다.
    ]
  ]
]
