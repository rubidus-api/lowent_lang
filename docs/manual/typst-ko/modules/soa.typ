#import "../lib.typ": *

= `soa` --- SoA 배치 시범: 필드마다 배열 하나 <mod-soa>

#modhead(file: "lib/soa.low", layer: [L0 --- 순수 계산], caps: [없음])

원소가 `{x, y, vx, vy}` 인 자료 n 개를 두는 방법은 둘이다. *AoS*(Array of Structs)는 한 배열에 원소를 통째로 늘어놓고, *SoA*(Struct of Arrays)는 *필드마다 배열
하나*를 나란히 둔다. x 만 훑는 계산이라면 SoA 쪽이 필요한 값만 연속으로 읽어 캐시를 알뜰하게 쓴다.

#aside[답이 아니라 측정이다][
  SoA 를 언어 기능(`store[T, soa]` 같은 타입 생성자와 키워드 · op 수십 개)으로 넣자는 조사는 *보류*됐고, 재검토 조건은 "먼저 `lib/` 에 빌트인 증가 0 으로 써 본다.
  거기서 막히는 곳이 언어 작업의 목록이 된다" 였다. 이 모듈이 그것이다. 발견 --- *SoA 배치 자체는 라이브러리로 완전히 표현된다*. 필드별 배열, 보폭 없는 순차
  접근, 한 필드만 훑는 커널이 전부 새 언어 기계 없이 써진다. 속도 주장은 측정 없이 하지 않는다 --- 이 모듈이 보장하는 것은 두 배치가 같은 답을 낸다는 정확성뿐이고,
  원소 전체를 만지는 코드는 AoS 가 낫다.
]

#dtable(
  columns: 3,
  id: "mod-soa-ops",
  caption: [`soa` 의 op --- 모두 `effects none`],
  [*op*], [*모양*], [*실패*],
  [`step_x`], [`proc (xs mut slice u64, vxs slice u64, n u64) → u64` --- `xs[i] += vxs[i]`], [없음 --- `min(n, len xs, len vxs)` 만 처리하고 그 수를 답한다],
  [`sum_field`], [`fn (f slice u64) → u64`], [없음],
  [`get_x`], [`fn (xs slice u64, i u64) → u64`], [범위 밖이면 `0`],
  [`step_all`], [`proc (xs, ys mut slice u64, vxs, vys slice u64, n u64) → u64`], [없음 --- 두 처리 수 중 작은 쪽],
  [`step_x_aos`], [`proc (rows mut slice u64, stride, xoff, voff, n u64) → u64` --- AoS 판], [속도 자리가 범위 밖인 원소에서 멈추고 그 `i` 를 답한다],
)

```lowent
proc demo input xs mut slice u64 . input vxs mut slice u64 .
  input rows mut slice u64 . output u64 . effects none .
do
  set (idx xs 0) 1 .
  set (idx xs 1) 2 .
  set (idx vxs 0) 10 .
  set (idx vxs 1) 20 .
  let n1 u64 soa.step_x xs vxs 2 .
  guard eq n1 2 else return 90 .
  let s1 u64 soa.sum_field (subslice xs 0 2) .
  set (idx rows 0) 1 .
  set (idx rows 1) 10 .
  set (idx rows 2) 2 .
  set (idx rows 3) 20 .
  let n2 u64 soa.step_x_aos rows 2 0 1 2 .
  guard eq n2 2 else return 91 .
  var s2 u64 add (idx rows 0) (idx rows 2) .
  guard eq s1 s2 else return 92 .
  return s1 .
end
```

*막히는 자리 --- 언어 작업의 목록.* ① 원소 하나를 "한 덩어리" 로 다루는 문법이 없다 --- 호출자가 필드를 손으로 모은다(`get_x`). 불편할 뿐 불가능하지 않다. ② 필드
개수만큼 인자가 늘어난다(`step_all`) --- 이것은 구조체 필드에 슬라이스를 허용하면서 풀렸지만, 이 모듈은 측정 기록이라 네 인자 모양을 그대로 둔다. ③ *타입이 배치를
모른다* --- AoS 판과 SoA 판이 서로 다른 op 이름이 되고(`step_x` 대 `step_x_aos`), AoS 의 오프셋 인자는 전부 `u64` 라 컴파일러가 지켜 주지 못한다.

#antipattern[반환된 처리 수를 보지 않는다][
  `soa.step_x xs vxs 1000` 은 `xs` 가 3 칸이면 조용히 3 개만 처리한다. `n` 개가 전부 처리됐다고 가정하는 코드는 `guard eq m n` 으로 확인한다.
]

#antipattern[AoS 판의 오프셋을 바꿔 낀다][
  `soa.step_x_aos rows 2 1 0 3` 은 xoff 와 voff 가 뒤집혀 에러 없이 속도에 위치가 더해진다. 배치가 타입에 실리지 않는다는 막힘의 실감이다.
]

*주의.* 나란한 배열들의 길이를 맞추는 것은 호출자 책임이다 --- op 은 짧은 쪽에 맞춰 줄일 뿐 알려 주지 않는다. `get_x` 의 실패 값 0 은 정상 값과 구분되지 않는다. 맨
`idx` 는 범위를 줄여 주지 않고 `E-VM-BOUNDS` 로 멈춘다. 긴 `let` · `set` 을 줄바꿈으로 나누면 개행이 form 을 닫는다 --- 이어 쓰려면 줄 끝에 `,` 를 둔다.
