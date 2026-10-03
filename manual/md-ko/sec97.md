# <a id="mod-tlscli"></a>`tlscli` — TLS 1.3 클라이언트 핸드셰이크

소스

`lib/tlscli.low`

층

L0 — 순수 계산(호출자의 뒷받침)

권한

없음

[`tlssrv`](sec96.md#mod-tlssrv) 의 **거울상**이다. 서버 쪽은 ClientHello 를 읽고 ServerHello 를 짓는다. 이 모듈은 반대로 ClientHello 를 **짓고** ServerHello 이하를 **읽는다**. 규격의 계산(키 스케줄 · 레코드 봉인)은 [`tls13`](sec95.md#mod-tls13) 에 있고, 이 모듈은 그것을 클라이언트의 순서대로 부르는 층이다.

> **이것만으로는 TLS 클라이언트가 아니다**
>
> > **전송이 없다** — 소켓도 재조립도 없고, 바이트열은 부르는 쪽이 모아 온다. 그래서 이 모듈은 `effects none` 이다. **인증서를 확인하지 않는다** — `Certificate` 메시지에서 인증서를 꺼내 줄 뿐이고, 체인 · 유효기간 · 이름은 [`x509`](sec93.md#mod-x509) · [`verify`](sec94.md#mod-verify) · [`trust`](sec120.md#mod-trust) 로 부르는 쪽이 확인한다. 확인하지 않는 도구는 그렇다고 말해야 한다(`lowget` 의 `--insecure`). 짓지 않은 것: HelloRetryRequest · PSK/0-RTT · 세션 재개 · 클라이언트 인증서 · x25519 가 아닌 키 교환 그룹 · `TLS_CHACHA20_POLY1305_SHA256` · `TLS_AES_128_GCM_SHA256` 이 아닌 스위트.

## <a id="sx1"></a>핸드셰이크의 차례

클라이언트는 한 가지 차례로만 메시지를 받는다. 이 차례를 지키지 않으면 중간자가 메시지를 빼거나 바꿔치기할 수 있다. `cnext_ok` 가 상태마다 **받아도 되는 메시지 하나**를 열거하고, 그 밖은 모두 거절한다.

```text
 cst_start ──ClientHello 보냄──▶ cst_wait_sh
 cst_wait_sh ──ServerHello(2)──▶ cst_wait_ee        ── 여기서 핸드셰이크 키를 만든다(hs_secrets)
 cst_wait_ee ──EncryptedExtensions(8)──▶ cst_wait_cert
 cst_wait_cert ──Certificate(11)──▶ cst_wait_cv     ── cert_at 으로 꺼내 부르는 쪽이 확인
 cst_wait_cv ──CertificateVerify(15)──▶ cst_wait_finished
 cst_wait_finished ──Finished(20)──▶ cst_connected  ── check_server_fin 이 참이어야 한다
```

괄호 안의 수는 핸드셰이크 메시지 종류 번호다. `cstep` 이 다음 상태를 낸다.

## <a id="sx2"></a>op

| **op** | **하는 일** |
|---|---|
| `cst_start` … `cst_connected` | 상태 번호(0 … 6) |
| `cnext_ok` · `cstep` | 지금 상태에서 이 메시지를 받아도 되는가 · 그다음 상태 |
| `build_ch` | ClientHello 를 짓는다 — 스위트는 ChaCha20-Poly1305 를 먼저, AES-128-GCM 을 다음에, 키 교환은 x25519 |
| `sh_ok` · `sh_usable` | ServerHello 가 성한가 · 우리가 이어 갈 수 있는 것인가(한 번에 판정) |
| `sh_is_hrr` | HelloRetryRequest 인가 — 알아보고 **거절한다** |
| `sh_suite` · `sh_is_tls13` · `sh_key_share_off` | 고른 스위트 · 정말 1.3 인가 · 서버의 x25519 공개값 자리 |
| `sh_ext_off` · `sh_ext_len` · `sh_ext_find` · `sh_ext_find_len` | 확장 묶음과 확장 하나 찾기 |
| `hs_secrets` | 핸드셰이크 비밀을 만든다(키 사다리) |
| `finished_vd` | 어떤 트래픽 비밀로든 Finished 의 검증값을 낸다 |
| `check_server_fin` | 서버의 Finished 를 검산한다 — «상대가 그 비밀을 정말 갖고 있는가» |
| `build_client_fin` | 우리 Finished 를 짓는다 |
| `seal_rec` | 레코드 하나를 봉한다(속 타입을 부르는 쪽이 정한다) |
| `plain_hdr` · `rec_len` · `rec_type` | 평문 레코드 머리를 짓는다 · 받은 레코드 머리를 읽는다 |
| `cert_at` · `cert_len` | Certificate 메시지에서 `n` 번째 인증서(0 이 잎)의 자리와 길이 |

*표 50.1 — `tlscli` 의 op*

## <a id="sx3"></a>설계

**읽는 쪽 길이는 전부 상대가 쓴 것이다.** ServerHello 이하는 아직 아무것도 인증되지 않은 바이트열이다. 어떤 자리도 버퍼를 넘으면 곧바로 0 을 답하고, 길이가 안 맞으면 고쳐 읽지 않는다.

**HelloRetryRequest 를 ServerHello 로 읽지 않는다.** HRR 은 ServerHello 와 같은 메시지 종류이고 랜덤 자리에 정해진 32 바이트가 들어가는 것으로만 구분된다. 그것을 모르면 HRR 의 랜덤을 진짜 랜덤으로 읽어 키 사다리를 **조용히 틀린 값**에서 쌓는다. HRR 을 짓지 않기로 했으므로 알아보고 거절한다.

**판을 고르는 것은 머리가 아니라 확장이다.** 메시지 머리의 `0303` 은 중간 상자를 속이려는 장식이다. 정말 1.3 인지는 `supported_versions` 확장이 `0304` 인지로 본다(`sh_is_tls13`).

**왜 ChaCha20 을 먼저 제안하나.** 이 언어로 쓴 두 암호의 처리량을 재어 빠른 쪽을 앞에 둔다. 서버가 순서를 존중하면 빠른 쪽을 고른다.

**`check_server_fin` 이 참이어도 «누구인지» 는 아직 모른다.** 그것은 «상대가 핸드셰이크 비밀을 갖고 있는가» 를 확인할 뿐이고, 그 상대가 누구인지는 인증서 쪽(`verify`)의 물음이다. 둘을 모두 확인해야 연결을 믿을 수 있다.

**왜 이 모듈이 있나.** 클라이언트가 있어야 바깥의 진짜 구현(`openssl s_server`, 실제 웹 서버)이 반대편에 설 수 있다. 우리 서버(`tlssrv`)와만 맞대면 양쪽이 같은 오해를 나눠 가질 수 있고, 그러면 «규격을 같게 읽었다» 는 확인이 되지 않는다. 이 모듈 위에 선 실물 도구가 `apps/lowget` 이다.

---

[← 이전](sec96.md) · [목차로](README.md) · [다음 →](sec98.md)
