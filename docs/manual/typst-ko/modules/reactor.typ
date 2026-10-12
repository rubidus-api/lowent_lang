#import "../lib.typ": *

= `reactor` --- 막지 않는 읽기와 쓰기 <mod-reactor>

#modhead(file: "lib/reactor.low", layer: [L2 --- 호스트(빌트인 연산의 감싸개)], caps: [세 op 모두 `cap io`])

통로 여럿을 *막지 않고* 읽고 쓴다. reactor 의 상태는 부르는 쪽이 준 자리에 산다 --- 이 모듈은 메모리를 얻지 않는다. 핸들은 `u64` 라 지어낼 수 있으므로
*권한(`cap io`)을 부를 때마다* 보인다. 빌트인 연산 `reactor_new` · `r_read` · `r_write` 를 그대로 넘기는 감싸개 셋이다.

```lowent
use reactor .

proc echo_once input k cap io . input buf mut slice u8 . output u64 . effects io . do
  let r option u64 reactor.reactor_new k. subslice buf. 0 32 . 1 . .
  guard is_some r. . else return 0 . .
  let n option u64 reactor.r_read k. some_value r. . 0 subslice buf. 32 64 . . .
  guard is_some n. . else return 0 . .
  return some_value n. . .
end .
```

#dtable(
  columns: 3,
  id: "mod-reactor-ops",
  caption: [`reactor` 의 op],
  [*op*], [*모양*], [*비고*],
  [`reactor_new`], [`(cap io, backing mut slice u8, n u64) → option u64`], [reactor 를 만든다. `backing` 은 그 상태가 살 자리, `n` 은 한 번에 다룰 통로의 수],
  [`r_read`], [`(cap io, r u64, d u64, dst mut slice u8) → option u64`], [통로 `d` 에서 읽는다. 지금 없으면 0, 읽지 못하면 `none`],
  [`r_write`], [`(cap io, r u64, d u64, b slice u8) → option u64`], [통로 `d` 에 쓴다. 쓴 바이트 수, 쓰지 못하면 `none`],
)

*짓지 않은 것* --- 타이머, 통로를 등록하고 빼는 op, 여러 통로를 한 번에 기다리는 op.
