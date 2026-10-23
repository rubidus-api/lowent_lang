#import "../lib.typ": *

= `env` --- 환경 변수 <mod-env>

#modhead(file: "lib/env.low", layer: [L2 --- 호스트(빌트인 연산의 감싸개)], caps: [`cap env`])

환경 변수는 프로그램 밖에서 동작을 조용히 바꾸는 통로다. 그래서 읽으려면 `cap env` 를 받아야 하고, 그 사실이 op 의 머리에 보인다(#chref("capabilities")).
빌트인 연산 `env_get` 을 그대로 넘기는 감싸개 하나다.

```lowent
use env .

proc has_home input e cap env . output bool . effects none . do
  let v option slice u8 env.get e. "HOME" . .
  return is_some v. . .
end .
```

#dtable(
  columns: 3,
  id: "mod-env-ops",
  caption: [`env` 의 op],
  [*op*], [*모양*], [*비고*],
  [`env_get`], [`(cap env, name slice u8) → option slice u8`], [그 이름의 변수 값. 변수가 없으면 `none`],
)

*짓지 않은 것* --- 환경 변수 쓰기 · 지우기, 전체를 훑기.
