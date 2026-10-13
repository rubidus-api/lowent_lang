#import "../lib.typ": *

= `stdio` --- 표준 입출력 <mod-stdio>

#modhead(file: "lib/stdio.low", layer: [L2 --- 호스트(빌트인 연산의 감싸개)], caps: [두 op 모두 `cap io`])

프로세스가 태어날 때 받는 세 통로 --- 0(입력) · 1(출력) · 2(오류) --- 에 닿는 두 op 이다. 화면에 쓰고 입력에서 읽는 일은 언어로 적을 수 없으므로 처리기가
*빌트인 연산*(`call_builtin write_out` · `call_builtin read_in`)으로 내놓고, 프로그램은 이 모듈의 op 을 부른다. 감싸개는 인자를 그대로 넘긴다 --- 네이티브
코드에서는 C 컴파일러가 펼치므로 값이 없다.

```lowent
use stdio .

proc main input out cap io . output u8 . effects io . do
  let n u64 stdio.write out. 1 "hello\n" . .
  return 0 .
end .
```

#dtable(
  columns: 3,
  id: "mod-stdio-ops",
  caption: [`stdio` 의 op],
  [*op*], [*모양*], [*비고*],
  [`write_out`], [`(cap io, d u64, b slice u8) → usize`], [통로 `d` 에 쓴다. *쓴 바이트 수* --- 파이프에서는 `len b. .` 보다 적을 수 있고, 0 은 "더는 못 쓴다" 다],
  [`read_in`], [`(cap io, d u64, dst mut slice u8) → option usize`], [통로 `d` 에서 읽는다. 0 은 입력의 끝, `none` 은 읽지 못한 것],
)

끝까지 쓰기(`write_all`)와 모아 쓰기는 #modref("outbuf")[`outbuf`] 에, 읽은 바이트를 줄로 자르는 일은 #modref("io")[`io`] 에 있다.

*짓지 않은 것* --- 서식 있는 출력(조립은 #modref("fmt")[`fmt`] 가 한다), 줄 단위 읽기, 통로 닫기.
