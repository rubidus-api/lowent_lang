#import "../lib.typ": *

= `outbuf` --- 버퍼링 출력, flush 망각은 컴파일 오류 <mod-outbuf>

#note[op 이름이 모두 `buf_` 로 시작하는 까닭: 이름공간이 평평해서 `open`·`write`·`finish` 는 `files` 가 이미 쓰는 이름이다. 두 모듈이 같은 이름을 내보내면 *한 프로그램에 같이 설 수 없다* --- 파일을 여는 프로그램이 버퍼링 출력을 못 쓰게 된다.]

#modhead(file: "lib/out.low", layer: [L2 --- 바깥 세계], caps: [`buf_flush` · `buf_write` · `buf_finish` 에 `cap io`])

출력을 *모아 두었다가 한 번에* 내보낸다. `write_out` 은 호출마다 비우므로 조각마다 부르면 조각마다 시스템 호출이다 --- 이 라이터는 바이트를 호출자 버퍼에 쌓았다가 가득 찰
때만 비워 시스템 호출 수를 바이트 수에서 버퍼 수로 줄인다. 그리고 마지막에 반드시 `finish` 를 부르게 만들어 "모아 둔 것을 내보내지 않고 끝내는" 사고를 *컴파일 단계에서*
막는다.

```lowent
var p be owned outbuf.pending outbuf.buf_open 1 .
let w be result (owned outbuf.pending) outbuf.io_error outbuf.buf_write out p buf "hi\n" .
guard is_ok w . else return 1 .
set p (ok_value w) .
let r be result void outbuf.io_error outbuf.buf_finish out p buf .
```

*소유되는 것은 버퍼가 아니라 아직 나가지 않은 바이트다.* 버퍼는 호출자 것으로 두고(#modref("fmt")[`fmt`] 와 같은 규율), `owned` 로 지키는 것은 비워지지 않은 상태
`pending` 이다 --- 잃으면 안 되는 바로 그것이 소유된다. `finish` 가 `owned pending` 을 받고 `result` 를 내므로, 그런 값을 조용히 버리면 `E-OWN-INCOMPLETE` 다
(#chref("ownership")). 어노테이션이 아니라 시그니처에서의 추론이다. 다른 언어에서는 flush 를 잊으면 출력이 통째로 사라지거나, 소멸자에서 flush 하되 오류를 버린다 ---
여기는 컴파일이 되지 않는다.

*라이터는 `cap io` 를 품지 않는다.* 권한은 값이 아니라 정적 토큰이라 구조체에 담을 수 없다. 그래서 바이트가 실제로 나가는 지점마다 호출자가 권한을 댄다 --- *라이터가
몰래 flush 할 수 없다*(#chref("capabilities")).

#dtable(
  columns: 3,
  id: "mod-outbuf-ops",
  caption: [`outbuf` 의 op],
  [*op*], [*모양*], [*실패*],
  [`write_all`], [`proc (out cap io, d u64, b slice u8) → option u64` --- 버퍼를 거치지 않고 한 조각을 *끝까지* 쓴다], [`none` --- 더는 못 쓴다],
  [`io_error` · `pending`], [enum `write_failed` · 구조체 `pos u64`(쌓인 바이트 수) · `fd u64`(1 = stdout, 2 = stderr)], [---],
  [`buf_open`], [`fn (d u64) → pending`, effects none], [없음 --- 다만 여는 순간 갚아야 할 빚이 생긴다],
  [`buf_flush`], [`proc (out cap io, p owned pending, buf mut slice u8) → result (owned pending) io_error`], [`error write_failed`],
  [`buf_write`], [`proc (out cap io, p owned pending, buf mut slice u8, s slice u8) → result (owned pending) io_error` --- 가득 차면 스스로 비운다], [`error write_failed`],
  [`buf_finish`], [`proc (out cap io, p owned pending, buf mut slice u8) → result void io_error` --- 꼬리를 비우고 완결], [`error write_failed`],
)

`buf_write` 와 `buf_flush` 는 소유를 소비하고 새 상태를 `ok` 로 돌려준다 --- 호출자는 `set p (ok_value w)` 로 매번 이어받는다. `set` 은 사용이 아니라 재초기화라 루프 안에서도 소유가
유지된다. *짧게 쓰인 것은 실패가 아니다* --- 잎(`write_out`)은 파이프가 차면 요청보다 적게 쓰고, 그것은 정상이다. `buf_flush` 는 `write_all` 로 *끝까지* 쓴다. 실패(`write_failed`)는 "더는 못 쓴다" 일 때만 나고, 그때 pending 은 소비되어 그 시점 버퍼의 바이트는 잃는다(재시도는 짓지 않았다). 버퍼가
작아도 동작은 맞고 크기는 속도에만 영향을 준다. `buf_flush` 를 직접 부를 일은 "이 줄이 당장 화면에 보여야 한다" 같은 때뿐이다.

```lowent
module emit .

use fmt .
use outbuf .

proc main input out cap io . input al cap allocator . output u8 . effects alloc io . do
  let g be option mut slice u8 alloc_bytes al capacity 16 .
  guard is_some g . else return 70 .
  let buf be mut slice u8 some_value g .
  let ng be option mut slice u8 alloc_bytes al capacity 32 .
  guard is_some ng . else return 71 .
  let nb be mut slice u8 some_value ng .
  var p be owned outbuf.pending outbuf.buf_open 1 .
  var i be u64 1 .
  while le i 5 . do
    let a be option u64 fmt.put_str nb 0 "line " .
    guard is_some a . else return 72 .
    let b be option u64 fmt.put_u64 nb (some_value a) i .
    guard is_some b . else return 73 .
    let c be option u64 fmt.put_nl nb (some_value b) .
    guard is_some c . else return 74 .
    let w be result (owned outbuf.pending) outbuf.io_error
      outbuf.buf_write out p buf (subslice nb 0 (some_value c)) .
    guard is_ok w . else return 75 .
    set p (ok_value w) .
    set i (add i 1) .
  end
  let f be result void outbuf.io_error outbuf.buf_finish out p buf .
  guard is_ok f . else return 76 .
  return 0 .
end
```

순수한 포매팅(`fmt`)이 바이트를 조립하고 버퍼링 출력(`outbuf`)이 내보낸다. 조립용 버퍼 `nb` 와 라이터의 버퍼 `buf` 는 따로다.

#antipattern[`finish` 생략 · 이동한 `p` 재사용][
  `owned pending` 을 완결하지 않고 반환하면 `E-OWN-INCOMPLETE` 다 --- 중간에서 `return` 으로 빠지는 경로도 마찬가지다. `write` 뒤 `set p …` 없이 옛 `p` 를 넘기면
  이동된 값의 사용으로 컴파일 오류다. 둘 다 컴파일러가 잡는다.
]

#antipattern[`write` · `finish` 사이에 버퍼를 다른 용도로 쓴다][
  `pending.pos` 는 그 버퍼에 쌓인 바이트 수를 가리킨다. 조립용 버퍼와 같은 것을 쓰면 컴파일은 통과하고 나가지 않은 바이트가 덮어써져 엉뚱한 내용이 나간다. 이 모듈에서
  조용히 틀리는 유일한 실수다 --- 라이터 하나에 버퍼 하나를 붙박이로 쓴다.
]

*주의.* 권한 없이 비우려 하면 컴파일 오류다(`effects io` 를 선언하고 권한을 받지 않으면 `E-EFFECT-NO-CAP`). fd 는 열림이 검사되지 않는 정수다. 출력이 이미 한 버퍼에 다
있으면 라이터가 필요 없다 --- `write_out` 한 번이면 된다. 벡터 쓰기(writev) · 실패한 flush 뒤의 재시도 · 중단 가능 출력은 짓지 않았다.
