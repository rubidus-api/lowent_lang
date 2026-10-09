#import "../lib.typ": *

= `net` --- 소켓(TCP loopback · 프로세스 안 한 쌍) <mod-net>

#modhead(file: "lib/net.low", layer: [L2 --- 바깥 세계], caps: [`cap net`])

소켓을 열고 · 주고받고 · 닫는다. 여는 길이 둘이다 --- *TCP*(127.0.0.1 로 듣고 연결한다)와 *한 쌍*(같은 프로세스 안의 두 끝, 주소가 없다). 모든 op 이 `cap net` 을
받는다. 세상에 닿는 능력은 건네받는 권리이지 어디선가 꺼내 오는 것이 아니다(#chref("capabilities")).

*핸들은 자원이다.* `conn` · `listener` · `pair` 는 닫아야 하고, *닫기는 실패할 수 있다*(`result void net_error`). 그래서 닫기를 잊으면 컴파일이 되지 않는다
(`E-OWN-INCOMPLETE`, #chref("ownership")).

```lowent
proc main input k cap net . output u8 . effects io . do
  let po option net.pair net.pair_of k. . .
  guard is_some po. . else return 1 . .
  var p owned net.pair some_value po. . .
  let s option u64 net.send_all k. net.pair_a p. . "hi" . .
  guard is_some s. . else return 2 . .
  let c result void net.net_error net.shut_pair k. p. . .
  guard is_ok c. . else return 3 . .
  return 0 .
end .
```

#dtable(
  columns: 3,
  id: "mod-net-ops",
  caption: [`net` 의 op],
  [*op*], [*모양*], [*실패*],
  [`serve`], [`(cap net, port u64) → option listener`], [`none` --- 듣지 못했다],
  [`dial`], [`(cap net, port u64) → option conn`], [`none` --- 붙지 못했다],
  [`take`], [`(cap net, l listener) → option conn`], [`none` --- 받아들이지 못했다],
  [`pair_of`], [`(cap net) → option pair`], [`none`],
  [`send_all`], [`(cap net, fd u64, buf slice u8) → option u64`], [*보낸 만큼*을 답한다],
  [`recv_once`], [`(cap net, fd u64, dst mut slice u8) → option u64`], [`some 0 .` = 상대가 닫음 · `none` = 실패],
  [`shut` · `shut_listener` · `shut_pair`], [`(cap net, owned X) → result void net_error`], [`error close_failed .`. 쌍은 두 끝을 *각각* 닫는다],
  [`conn_fd` · `pair_a` · `pair_b`], [`→ u64`], [*순수* --- 권한이 필요 없다],
)

듣는 포트는 `field l. port .` 로 읽는다(임시 포트를 받았으면 그 번호). fd 를 읽는 것은 순수하다 --- 자원은 잊지 말아야 할 것이지만, 번호를 들여다보는 데는 권한이 들지
않는다.

*알아 둘 계약 둘.* `send_all` 은 끝까지 보내되 거짓말하지 않는다 --- 한 번의 송신이 전부를 보낸다는 보장이 없으므로 반복하고, 도중에 막히면 *거기까지*를 답한다.
`some n. .` 을 받으면 `n` 이 `len buf. .` 와 같은지 확인한다. `recv_once` 의 `0` 은 실패가 아니라 상대가 닫았다는 *사실*이다.

*셋을 한 번에 닫는 op 을 없앤 이유.* 예전에는 핸들이 정수였고 `shut3` 이 fd 셋을 한 번에 닫았다. 듣기 · 연결 · 받아들인 것은 서로 다른 자원이고, 셋을 한 호출로 뭉치면
컴파일러가 *무엇을 잊었는지* 말할 수 없다. 편의가 안전을 가리면 그 편의는 값이 아니다. 지금 쌍은 두 끝을 각각 `owned conn` 으로 품어, 한쪽만 닫고 잊어도 컴파일되지
않는다.

*시험하기 --- 실패를 불러온다.* `LOW_HOST_FAULT="connect:err"`(연결 거부), `"accept:err"`, `"send:err@1"`(첫 송신 실패), `"recv:short=2"`(수신을 2 바이트로 자름). VM 과
네이티브가 같은 주입기를 쓰므로 두 백엔드의 답이 같아야 한다(#chref("io-files")).

*짓지 않은 것* --- 원격 대상(loopback 만 있다), 타임아웃, 비차단 · 폴링, UDP, 주소 파싱.
