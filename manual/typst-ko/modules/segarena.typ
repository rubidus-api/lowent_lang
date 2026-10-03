#import "../lib.typ": *

= `segarena` --- 고정 크기 세그먼트 아레나 <mod-segarena>

#modhead(file: "lib/segarena.low", layer: [L1 --- 호출자의 저장], caps: [없음])

저장소를 한 덩어리로 잡지 않고 *같은 크기의 조각(segment)* 으로 늘려 간다. 조각 안은 연속이라 훑기가 빠르고, 자라날 때 이미 있는 것을 옮기지 않는다. 색인 하나가
조각과 자리로 갈린다 --- 그게 전부다.

```text
segment = i >> kbits          slot = i & ((1 << kbits) - 1)
address = directory[segment] + slot
```

```lowent
def newtype grid u8 .
var a be (segarena.arena grid) segarena.open grid 4 .
let b be option u64 segarena.grow grid a dir 1024 .
guard is_some b . else return 1 .
let v be option u64 segarena.at grid a dir mem 9 .
```

#aside[비용이 op 이름에 적혀 있다 --- 이것이 이 모듈의 요점이다][
  같은 합을 2,048 만 원소에서 두 방법으로 쟀다. 조각을 한 번 빌리는 `sum_seg` 는 조각마다 directory 를 한 번 보고 21 ms, 전역 색인으로 매번 읽는 `sum_global` 은 원소마다
  보고 81 ms 였다. 답은 같다. 컴파일러는 세그먼트를 모른다 --- 빠른 쪽을 고르는 것은 *당신이 부르는 op* 이다. 전역 임의 접근의 비용을 평평한 slice 처럼 숨기지 않는다.
]

#dtable(
  columns: 3,
  id: "mod-segarena-ops",
  caption: [`segarena` 의 op],
  [*op*], [*하는 일*], [*실패하면*],
  [`arena`], [아레나 타입(커서와 모양만 든다)], [---],
  [`open`], [아레나를 연다(조각당 `2^kbits` 칸)], [계약 `1 ≤ kbits ≤ 20` --- 상수면 컴파일 때 거절],
  [`grow`], [조각을 하나 더 열고 `dir` 에 시작 자리를 적는다], [자리가 없거나 용량을 넘으면 `none`],
  [`seg_slots` · `seg_of` · `slot_of`], [조각당 칸 수 · 전역 색인 → 조각 · 자리], [---],
  [`at`], [전역 색인으로 한 칸 읽기(편하다, directory 를 매번 본다)], [범위 밖 · 열지 않은 조각이면 `none`],
  [`walk_base`], [조각의 시작 자리를 한 번 빌린다(뜨거운 순회용)], [열지 않은 조각이면 `none`],
  [`sum_seg` · `sum_global`], [같은 합을 두 경로로 --- 비용 대조의 증인(주장이 참임을 보여 주는 자리)], [---],
)

#antipattern[조각 크기가 뜻이 없다][
  `segarena.open g 0` 은 컴파일 에러 `E-CONTRACT-IMPOSSIBLE` 이다 --- 조각이 한 칸이면 세그먼트가 아니다. 인자가 상수라 부르는 자리에서 판정된다(#chref("contracts")).
]

#antipattern[뜨거운 순회를 전역 색인으로 돈다][
  `while` 안에서 원소마다 `segarena.at` 을 부르면 에러는 없고 느릴 뿐이다(측정에서 3.9 배). 조각 단위로 돌면서 `walk_base` 로 시작 자리를 한 번만 빌린다.
]

*주의.* 저장은 호출자 것이다 --- `dir`(조각마다 시작 자리)과 `mem`(칸이 사는 곳)을 밖에서 준다(#modref("pool")[`pool`] · #modref("shard")[`shard`] 와 같은 규율).
회수는 일괄이다 --- 조각 하나만 돌려주는 길은 없다. 개별 회수가 필요하면 `pool` 이 그 자리다. 가변 크기 조각은 짓지 않는다 --- `i >> k` 공식이 서지 않고 비용이
달라지므로 다른 모듈이어야 한다. 조각을 빌리는 뷰는 #modref("segview")[`segview`] 가 맡는다.
