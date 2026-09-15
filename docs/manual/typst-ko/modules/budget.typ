#import "../lib.typ": *

= `budget` --- 핸들 비트 예산과 세대 한 바퀴 <mod-budget>

#modhead(file: "lib/budget.low", layer: [L1 --- 순수 계산], caps: [없음])

세대 핸들을 *한 워드*에 넣을 때 세 조각에 몇 비트씩 줄지 정하고, 그 예산이 들어맞는지 *컴파일 때* 확인한다. 저장소를 직접 만들 때 --- #modref("pool")[`pool`] ·
#modref("shard")[`shard`] 처럼 칸을 나눠 주고 되받는 것을 지을 때 --- 핸들의 폭을 소스에 적고 도구가 지키게 하는 자리다.

```text
h = (generation << (shard_bits + slot_bits)) | (shard << slot_bits) | slot
      몇 번째 삶                     어느 샤드                  어느 칸
```

```lowent
let ho option u64 . be budget.pack 32 8 24 7 1 0 .
guard is_some ho . else return 1 .
let h u64 be some_value ho .
let slot u64 be budget.slot_of 32 h .
```

#aside[이 모듈이 지키는 것][
  *들어가지 않는 예산은 컴파일되지 않는다.* `budget.pack 40 16 16 …` 은 합이 72 라 한 워드를 넘는다 --- 상수뿐이므로 부르는 자리에서 `E-CONTRACT-IMPOSSIBLE` 이다.
  *값이 자기 칸에 들어가지 않으면 `none`* 이다 --- 조용히 잘라 담으면 서로 다른 두 핸들이 같아진다. *세대가 한 바퀴 돌면 그 칸은 은퇴한다* --- `next_gen` 이
  `none` 을 답하고 되살리지 않는다. 0 으로 돌아가면 그 칸의 옛 핸들이 전부 되살아나는데, 그것이 세대 핸들이 막으려던 바로 그 일이다.
]

#dtable(
  columns: 4,
  id: "mod-budget-defaults",
  caption: [권고 기본값과 근거],
  [*기계*], [*slot · shard · gen*], [*담는 수*], [*근거*],
  [64 비트], [32 · 8 · 24], [약 43 억 칸 · 256 샤드 · 약 1,700 만 삶], [널리 쓰이는 실물 id 중 가장 넓은 것(SQLite 페이지)이 32 비트],
  [32 비트], [16 · 4 · 12], [65,536 칸 · 16 샤드 · 4,096 삶], [lwIP 가 길이를 16 비트로 센다],
)

기본값일 뿐이다 --- 저장소가 자기 수를 적으면 그 수가 이긴다.

#dtable(
  columns: 3,
  id: "mod-budget-ops",
  caption: [`budget` 의 op],
  [*op*], [*모양*], [*실패하면*],
  [`plan` · `default64` · `default32`], [예산 구조체 · 권고 기본값], [---],
  [`pow2`], [`n u64 → u64`(계약 `n < 63`)], [계약 위반은 진입에서 멈춘다],
  [`slots` · `shards` · `lives`], [`p plan → option u64`], [폭이 63 이상이면 `none`],
  [`pack`], [예산 셋 + 값 셋 → `option u64`], [값이 칸을 넘으면 `none` · 예산이 넘치면 컴파일 에러],
  [`slot_of` · `shard_of` · `gen_of`], [예산 + `h` → `u64`], [계약 위반은 진입에서 멈춘다],
  [`retired`], [`gen_bits, gen → bool`], [---],
  [`next_gen`], [`gen_bits, gen → option u64`], [은퇴한 칸이면 `none`],
)

#antipattern[세대를 그냥 올린다][
  `let g u64 be add gen 1 .` 은 에러가 없지만, 폭을 넘으면 다른 삶의 핸들과 같아진다. `next_gen` 을 쓰고 `none` 을 받는다 --- 그 칸은 끝났다는 뜻이다.
]

#antipattern[63 비트 칸을 센다][
  `budget.pow2 63` 은 진입 계약이 거절한다. `shl 1 63` 은 부호 있는 64 비트에서 넘쳐 음수가 된다 --- 조용히 틀린 수를 돌려주느니 거절한다.
]

*주의.* 되꺼낼 때 *같은 예산*을 준다 --- `pack` 과 `slot_of` 가 다른 수를 보면 답이 조용히 틀린다. 예산을 상수 셋으로 한 자리에 적어 두고 그것만 쓴다. op 이름이
`cap` 이 아니라 `pow2` 인 이유 --- `cap` 은 문법 자리(`input k cap clock .`)라 op 이름으로 쓰면 `clock` 과 한 단위에 설 수 없었다. 두 워드 핸들은 짓지 않았다.
