# `lowget` 매뉴얼

## 부르는 법

```
lowget <url> [-i] [-L] [--insecure] [--cafile <path>]
```

첫 인자는 **언제나 url** 이고, 낱말들은 그 뒤에 온다(`lowget https://… -L`).

| 낱말 | 뜻 |
|---|---|
| `-i` | 머리줄도 함께 낸다(기본은 본문만) |
| `-L` | 3xx 를 **따라간다**(최대 여덟 번) — 마디마다 인증서를 다시 확인한다 |
| `--insecure` | 인증서를 **확인하지 않는다**. 그러면 도구가 stderr 에 그렇게 적는다 |
| `--cafile <길>` | 신뢰 저장소를 갈아탄다(기본 `/etc/ssl/certs/ca-certificates.crt`) |

☞ `-o <파일>` 은 **없다.** `lowget … > 파일` 과 하는 일이 정확히 같다(진단은 이미 stderr 로
간다). 같은 일을 두 자리에서 하면 둘이 언젠가 달라진다.

## 짓는 법

```sh
impl/build/lowentc --emit-c apps/lowget/src/lowget.low > /tmp/lowget.c
cc -O2 -o /tmp/lowget /tmp/lowget.c -lm
/tmp/lowget https://example.com/
```

## 종료 코드

| 코드 | 뜻 |
|---|---|
| 0 | 성공 |
| 1 | HTTP 오류 상태(400 이상) |
| 2 | 사용법 |
| 3 | 연결·이름·따라갈 수 없는 자리 |
| 4 | **인증서를 믿지 못한다** |
| 5 | 규약 위반 |

안쪽의 자세한 수(어디서 멈췄는가)는 **stderr 로만** 나간다: `lowget: … (step 94)`.
종료 코드는 위의 여섯 가지뿐이다 — 부르는 쪽이 세어야 할 것을 늘리지 않는다.

## 무엇을 확인하나

| 봄 | 어떻게 |
|---|---|
| 이름 | SAN dNSName, 와일드카드는 **한 단계**까지 |
| 기간 | `cap clock` 이 준 지금이 notBefore..notAfter 안에 드는가 |
| 체인 | 마디마다 발급자 이름이 **바이트로** 이어지고 **서명이 맞는가** |
| CA | 서명한 쪽이 CA 라고 적혀 있는가(basicConstraints 없으면 CA 아님) |
| 뿌리 | 체인이 신뢰 저장소 안의 인증서에 닿는가 |

서명 알고리즘은 넷을 확인한다: RSA(PKCS#1 v1.5)·ECDSA 를 SHA-256 과 SHA-384 으로.
**그 밖은 거절한다.**

거절하면 **왜인지 말한다**:

```
lowget: the certificate is not for this host (no matching name)
lowget: the certificate is outside its validity dates
lowget: the chain does not hold — a link failed (…)
lowget: the chain does not reach a certificate in the trust store
lowget: the server sent no certificate this build could read
```

「인증서 오류」 한 마디는 쓰는 사람에게 아무것도 주지 못한다.

## 본문이 어디서 끝나는지 **셋으로** 안다

청크의 `0` 덩이 · `Content-Length` · 연결이 닫히는 것. 앞의 둘을 안 보면
`Connection: close` 를 안 지키는 서버에서 **매달리고**, 매달림은 「느린 성공」과
구별되지 않는다.

- `chunked` 와 `Content-Length` 가 **둘 다** 적힌 답에서는 청크가 이긴다(RFC 9112 §6.1).
- `204`·`304` 는 길이 줄이 없어도 본문이 없다(RFC 9110 §15.3.5).
- `#조각`은 서버에 보내지 않는다(RFC 3986 §3.5).
- `User-Agent: lowget/1.0` 과 `Accept: */*` 를 적는다 — 없으면 403 으로 막는 서버가 있다.

## 안 지은 것 (정직하게)

- **HTTP/2·HTTP/3 없음.** ALPN 을 제안하지 않으므로 서버가 HTTP/1.1 로 답한다.
- **압축 없음.** `Accept-Encoding` 을 보내지 않는다 — 받은 바이트가 곧 본문이다.
- **세션 재개·0-RTT 없음.** `NewSessionTicket` 은 순번만 세고 버린다.
- **폐기 확인(CRL·OCSP) 없음.** 기간과 체인은 보지만 «취소되었는가» 는 묻지 않는다.
- **IPv6 없음.** 이름을 풀 때 첫 A 레코드를 쓴다.
- 쿠키·인증·POST 없음. 이 도구가 하는 일은 `GET` 하나다.

## 바깥 오라클

- `curl -sS <url>` 과 **본문 바이트**를 맞댄다.
- 골든은 바깥 망을 쓰지 않는다 — 로컬 루프백에서 `openssl s_server -tls1_3` 를 반대편에
  두고 통과 셋(평문·청크·`-L`)과 **거절 다섯**을 잰다.
