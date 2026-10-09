#import "../lib.typ": *

= `random` --- 난수(재현되는 열 · OS 엔트로피) <mod-random>

#modhead(file: "lib/random.low", layer: [L0 --- 순수 계산 · OS 엔트로피는 권한으로], caps: [`bytes` · `seed_from_os` 에 `cap random`])

#aside[둘을 섞지 않는다 --- 이 모듈의 전부가 이 경고다][
  `advance_seed` · `below_biased` · `coin` 은 *splitmix64* 로 만든 재현 가능한 열이다. 권한이 없고 시험 · 시뮬레이션 · 셔플에 쓴다. `bytes` · `seed_from_os` 는 *OS
  엔트로피*다. `cap random` 이 필요하고 열쇠 · 논스 · 토큰에 쓴다. 시험은 재현되어야 하고 열쇠는 예측되면 안 된다 --- 한 낱말로는 둘 다 못 하므로 이름을 갈라
  두었다. `advance_seed` 으로 만든 값을 열쇠로 쓰면 시드를 아는 사람이 그 열쇠를 알고, 시드는 보통 코드나 로그에 남는다.
]

*재현되는 쪽.* 상태(= 시드)를 받아 다음 상태를 돌려준다. 호출자가 상태를 들고 다니므로 같은 시드는 언제나 같은 열을 낸다.

```lowent
var s u64 12345 .
set s. random.advance_seed s. . .
let c bool random.coin s. . .
```

*예측할 수 없는 쪽.* `cap random` 을 받아야 부를 수 있다. 채운 바이트 수를 답하고 *못 채우면 0* 이다 --- 0 을 받고 그대로 쓰면 초기화되지 않은 버퍼를 열쇠로 쓰는
것이다.

```lowent
proc make_key input k cap random . input key mut slice u8 . output bool . effects none . do
  return eq random.bytes k. key. . len key. . . .
end .
```

#dtable(
  columns: 3,
  id: "mod-random-ops",
  caption: [`random` 의 op],
  [*op*], [*모양*], [*비고*],
  [`advance_seed`], [`(seed u64) → u64`], [splitmix64 --- 알고리즘 고정(고정해야 검사값이 기준이 된다)],
  [`below_biased`], [`(seed u64, bound u64) → u64`], [이름이 *편향을 자백한다* --- 균등이 필요하면 이것이 아니다],
  [`coin`], [`(seed u64) → bool`], [앞 · 뒤],
  [`bytes`], [`(cap random, dst mut slice u8) → u64`], [채운 바이트 수. *0 = 실패*],
  [`seed_from_os`], [`(cap random, scratch mut slice u8) → u64`], [OS 에서 받은 시드 하나 --- 재현되는 열의 출발점을 예측할 수 없게. `scratch ≥ 8`],
)

`below_biased` 의 이름에 `biased` 가 있는 이유 --- 나머지 연산으로 범위를 줄이면 작은 값이 조금 더 자주 나온다. 그것을 감추지 않고 이름에 적었다.

*실패를 불러와 시험한다.* 환경 변수 `LOW_HOST_FAULT="random:err"` 는 엔트로피를 받지 못하게(→ 0), `LOW_HOST_FAULT="random:short=3"` 은 3 바이트만 채우게 한다.
*부분 채움이 조용히 무서운 쪽이다* --- 버퍼 절반이 예전 값인 채로 열쇠가 된다. 그래서 채운 수를 확인하는 것이 계약이다(#chref("io-files")).

*짓지 않은 것* --- 균등 범위 난수(거절 표본추출), 셔플, 분포(정규 등), 재현되는 열의 직렬화, 암호용 CSPRNG(`bytes` 는 OS 에 묻는 것이지 이 모듈이 만드는 것이
아니다).
