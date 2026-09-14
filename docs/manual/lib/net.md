# net — 소켓 (TCP loopback · 프로세스 안 한 쌍)

소스: `lib/net.low` · 모듈명 `net` (RFC-0069 · 2026-08-07, **2026-08-15 에 표면이 바뀌었다**)

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 소켓을 열고·주고받고·닫는다. 여는 길이 둘이다 — **TCP**(127.0.0.1 로
듣고 연결한다)와 **한 쌍**(같은 프로세스 안의 두 끝, 주소가 없다).

**권한이 먼저다.** 모든 op 이 `cap net` 을 받는다. 그것을 안 받은 코드는 이 모듈을 부를 수
없다 — 세상에 닿는 능력은 **건네받는 권리**이지 어디선가 꺼내 오는 것이 아니다(RFC-0030 D2).

**핸들은 자원이다.** `conn`·`listener`·`pair` 는 닫아야 하고, **닫기는 실패할 수 있다**
(`result void net_error`). 그래서 잊으면 **컴파일이 안 된다**(`E-OWN-INCOMPLETE`).

**최소 예제 — 한 쌍으로 왕복.**

```lowent
use net .

proc main output u8 . input k cap net . effects io . do
  let po option net.pair . be net.pair_of k .
  guard is_some po . else return 1 .
  var p owned net.pair be some_value po .          rem ★ owned — 안 적으면 E-OWN-BARE

  let s option u64 . be net.send_all k (net.pair_a p) "hi" .
  guard is_some s . else return 2 .

  let c result void net.net_error . be net.shut_pair k p .   rem ★ 안 부르면 컴파일 실패
  guard is_ok c . else return 3 .
  return 0 .
end
```

## 2026-08-15 에 무엇이 바뀌었나

| | 전 | 지금 |
|---|---|---|
| 핸들 | **정수**(`u64`) — `serve` 는 `fd << 16 \| port` 로 **패킹**했다 | `conn` · `listener`(fd + port 두 필드) · `pair` |
| 닫기 | `shut … → bool` · `shut3`(fd 셋을 한 번에) | `shut`/`shut_listener`/`shut_pair` 가 `owned X → result void net_error` |
| 안 닫으면 | **아무 일도 없다** | `E-OWN-INCOMPLETE` — **컴파일 실패** |
| 푸는 짝 | `fd_of` · `port_of` | **사라졌다**(필드가 그 자리다) |

★ `shut3` 을 없앤 이유: 듣기·연결·받아들인 것은 **서로 다른 자원**이고, 셋을 한 호출로 뭉치면
컴파일러가 *무엇을 잊었는지* 말할 수 없다. **편의가 안전을 가리면 그 편의는 값이 아니다.**

★★ 쌍은 두 끝을 각각 `owned conn` 으로 품는다 — **한쪽만 닫고 잊어도** 컴파일이 안 된다.

## 표면

| op | 종류 | 모양 | 실패 |
|---|---|---|---|
| `serve` | proc | `(cap net, port u64) → option listener` | `none` — 못 들었다 |
| `dial` | proc | `(cap net, port u64) → option conn` | `none` — 못 붙었다 |
| `take` | proc | `(cap net, l listener) → option conn` | `none` — 못 받아들였다 |
| `pair_of` | proc | `(cap net) → option pair` | `none` |
| `send_all` | proc | `(cap net, fd u64, buf slice u8) → option u64` | **보낸 만큼**을 답한다(아래) |
| `recv_once` | proc | `(cap net, fd u64, dst mut slice u8) → option u64` | `0` = 상대가 닫음 |
| `shut` | proc | `(cap net, c owned conn) → result void net_error` | `error close_failed` |
| `shut_listener` | proc | `(cap net, l owned listener) → result void net_error` | 〃 |
| `shut_pair` | proc | `(cap net, p owned pair) → result void net_error` | 〃(두 끝을 **각각** 닫는다) |
| `conn_fd` · `pair_a` · `pair_b` | fn | `→ u64` | **순수** — 권한이 필요 없다 |
| `field l port` | — | 듣는 포트(임시포트를 받았으면 그 번호) | — |

★ **fd 를 읽는 것은 순수하다.** 자원은 잊지 말아야 할 것이지만, 그 번호를 들여다보는 데는
아무 권한도 들지 않는다 — 그 갈래가 소켓에 타입을 준 이유다.

## 알아 둘 계약 둘

**`send_all` 은 끝까지 보내되 거짓말하지 않는다.** `net_send` 가 한 번에 전부를 보낸다는 보장은
**없다** — 그것이 소켓의 성질이다. 그래서 이 op 은 반복하고, 도중에 막히면 **거기까지**를 답한다.
`some n` 을 받으면 **`n` 이 `len buf` 와 같은지 확인해야 한다**. 전량 성공만 성공이라 부르지
않는 대신, 부르는 쪽이 세어야 한다.

**`recv_once` 의 `0` 은 실패가 아니다.** 상대가 닫았다는 **사실**이다. 실패는 `none` 이다.

## 시험하기 — 실패를 불러온다

```
LOW_HOST_FAULT="connect:err"    연결이 거부된다
LOW_HOST_FAULT="accept:err"     받아들이기가 실패한다
LOW_HOST_FAULT="send:err@1"     첫 송신이 실패한다
LOW_HOST_FAULT="recv:short=2"   수신이 2 바이트로 잘린다
```

VM 과 방출 네이티브가 **같은 주입기**를 쓰므로 두 등의 답이 같아야 한다 — 골든이 그것을
요구한다. 기본은 꺼짐이다(`docs/operations/verification-model.md`).

## 안 지은 것 (정직히)

원격 대상(loopback 만 있다) · 타임아웃 · 비차단/폴링(그쪽은 reactor 의 몫) · UDP ·
주소 파싱. 그리고 `listen`/`port`/`net_close` 잎은 아직 결함 주입 대상이 아니다.
