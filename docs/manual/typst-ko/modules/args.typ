#import "../lib.typ": *

= `args` --- 명령줄 인자 <mod-args>

#modhead(file: "lib/args.low", layer: [L2 --- 호스트(빌트인 연산의 감싸개)], caps: [`cap args`])

프로그램이 받은 명령줄 인자를 읽는다. 인자는 어디서나 보이는 값이 아니라 `main` 이 받은 *권한*이다 --- 그래서 첫 입력이 `cap args` 다(#chref("capabilities")).
빌트인 연산 `arg` 를 그대로 넘기는 감싸개 하나다.

```lowent
use args .

proc first_len input a cap args . output u64 . effects none . do
  let who option slice u8 args.arg a. 0 . .
  guard is_some who. . else return 0 . .
  return len some_value who. . . .
end .
```

#dtable(
  columns: 3,
  id: "mod-args-ops",
  caption: [`args` 의 op],
  [*op*], [*모양*], [*비고*],
  [`arg`], [`(cap args, i u64) → option slice u8`], [`i` 번째 인자(0 부터). 그 자리에 인자가 없으면 `none`],
)

인자의 수는 기본 연산 `count a. .` 가 낸다.

*짓지 않은 것* --- 옵션 파싱(`--name value`), 인자를 수로 읽기.
