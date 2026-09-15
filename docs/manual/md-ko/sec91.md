# <a id="mod-tls13"></a>`tls13` — TLS 1.3 의 계산 부품

소스

`lib/tls13.low`

층

L0 — 순수 계산(호출자의 뒷받침)

권한

없음

TLS 1.3 이 요구하는 순수 계산 넷이다(RFC 8446) — **키 스케줄**(공유 비밀과 전사 해시에서 트래픽 키를 유도, §7.1), **레코드 계층**(레코드를 봉하고 연다, §5), **전사 해시**(지금까지 오간 메시지 전부의 해시, §4.4.1), **Finished**(핸드셰이크가 중간에 바뀌지 않았음을 증명, §4.4.4). 전부 순수해서 전송 없이 지을 수 있고 시험 벡터로 잴 수 있다 — 파서를 먼저, 전송을 나중에.

> **이것은 TLS 구현이 아니다**
>
> > 상태 기계가 없다 — 핸드셰이크를 구동하지 않는다(메시지 층과 순서는 [`tlssrv`](sec92.md#mod-tlssrv) 가 한다). 전송이 없다. PSK · 0-RTT · exporter · resumption 비밀이 없고 1-RTT 한 갈래만 지었다. PKI 가 없다([`der`](sec89.md#mod-der)). 상수 시간을 약속하지 않고 감사받지 않았다. 스위트는 둘뿐이고 모르는 스위트는 거절한다.

```text
0 ─HKDF-Extract(PSK)→ Early Secret ─Derive-Secret("derived","")→ ┐
ECDHE ─HKDF-Extract────────────────→ Handshake Secret ←──────────┘
  ├─ Derive-Secret("c hs traffic", CH..SH)
  └─ Derive-Secret("s hs traffic", CH..SH)
─Derive-Secret("derived","")→ ┐
0 ─HKDF-Extract──────────────→ Master Secret
  ├─ Derive-Secret("c ap traffic", CH..server Finished)
  └─ Derive-Secret("s ap traffic", CH..server Finished)
```

각 비밀에서 레코드 키가 나온다 — `key = Expand-Label(비밀, "key", "", 길이)`, `iv = Expand-Label(비밀, "iv", "", 12)`.

| **op** | **하는 일** |
|---|---|
| `build_label` · `expand_label` | HkdfLabel 구조체를 바이트로 · `HKDF-Expand-Label` |
| `derive_secret` | `Derive-Secret(비밀, 라벨, 전사 해시)` |
| `advance` | 사다리 한 칸 — 위 그림의 화살표 하나 |
| `traffic_key` · `traffic_iv` | 비밀 → 레코드 키 · IV |
| `finished_key` · `verify_data` | Finished 의 키와 값 |
| `record_header` · `record_nonce` | 5 바이트 헤더 `23 ‖ 0x0303 ‖ 길이` · IV 와 시퀀스 번호 → 논스 |
| `record_seal` · `record_open` | 레코드 봉하기 · 열기 |
| `inner_type` | 속 평문의 **끝**에서 진짜 내용 타입을 읽는다 |
| `transcript` | 이어 붙인 메시지 버퍼의 해시 |
| `hs_type` · `hs_size` · `hs_count` | 핸드셰이크 메시지 걷기 |
| `check_finished` | 상대의 Finished 를 다시 계산해 맞춰 본다 |

*표 50.1 — `tls13` 의 op*

**레코드는 겉과 속이 다르다.** 겉은 언제나 `23 ‖ 0x0303 ‖ 길이` 다 — 내용이 핸드셰이크든 응용 데이터든 똑같이 보인다. 진짜 내용 타입은 속 평문의 끝에 있다. 관찰자에게 감추려고 그렇게 짰다. AAD 는 그 5 바이트 헤더 자체다.

**스위트가 둘이라 협상이 뜻을 갖는다.** 스위트 1 = `TLS_AES_128_GCM_SHA256`(MUST, [`gcm`](sec83.md#mod-gcm)), 스위트 2 = `TLS_CHACHA20_POLY1305_SHA256`(SHOULD, [`aead`](sec80.md#mod-aead)). 하나만 지었을 때 이 자리는 죽은 분기였다. 모르는 스위트는 거절한다 — 조용히 하나를 고르지 않는다.

**전사 해시가 핸드셰이크의 뼈대다.** 키도 Finished 도 “지금까지 오간 메시지 전부” 의 해시 위에 서므로 중간자가 한 바이트라도 바꾸면 양쪽 키가 갈린다. **전사를 어디서 자르는지가 규격의 절반이다** — RFC 8448 이 각 Derive-Secret 의 해시로 그 자리를 적어 두었고, 시험이 세 자리(`CH…SH`, `CH…서버 Finished`, `CH…클라이언트 Finished`)를 전부 맞댄다. SHA-256 이 한 번에 하는 방식(스트리밍 없음)이라 전사는 **호출자가 이어 붙인 버퍼** 위에서 잰다. 메시지 걷기는 길이 필드를 믿지 않는다 — 버퍼를 넘는 길이가 적혀 있으면 거기서 멈춘다. `check_finished` 의 비교는 32 바이트를 XOR 로 누적해 마지막에 한 번 본다(조기 반환 없음).

**작업 공간을 둘로 묶는 이유.** 파라미터 상한이 16 이라 스위트마다 버퍼를 늘어놓으면 금방 넘는다. 그래서 바이트 `w` 와 `u64` `u` 둘로 묶고 자리를 소스 주석에 적어 나눠 쓴다. 예쁘지 않다 — 언어의 상한이 만든 모양이고, 그 사실을 감추지 않는 편이 낫다.

**확인하는 것 — 두 겹이다.** 파이썬 `hashlib` · `hmac` 로 §7.1 을 독립 구현해 바이트 대조하고, RFC 8448 §3 의 값들을 정본에서 기계로 뽑아 96 자리를 맞댄다. **그리고 정본이 결함을 하나 잡았다.** `traffic_key` 가 키 길이를 32 로 박아 두었는데, 그 길이는 HkdfLabel 안에 들어가므로 AES-128-GCM(16)을 쓰는 상대와는 키가 통째로 달라진다. 두 번째 구현으로는 잡히지 않았다 — 같은 사람이 같이 32 를 썼기 때문이다. 두 번째 구현은 정본이 아니다.

---

[← 이전](sec90.md) · [목차로](README.md) · [다음 →](sec92.md)
