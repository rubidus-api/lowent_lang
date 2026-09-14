# pem — PEM 봉투 벗기기

소스: `lib/pem.low` · 모듈명 `pem` (RFC 7468 · RFC-0090 §N7)

> ## ⚠ 무엇을 약속하고 무엇을 안 하나
>
> · **암호화된 PEM 을 못 연다**(`Proc-Type: 4,ENCRYPTED`). 열쇠로 잠긴 열쇠는 여기 없다.
> · **첫 번째 것만** 낸다. 한 파일에 여러 개(체인!)가 있으면 뒤엣것은 부르는 쪽이 다시 부른다.
> · url-safe base64 없음 · 검증 없음(**이것은 파서이지 신뢰 판단이 아니다**).

## 처음 쓰는 사람에게

**무엇을 하는 모듈인가.** 이런 파일에서 **가운데 바이트**를 꺼낸다.

```
-----BEGIN CERTIFICATE-----
MIIBczCCARqgAwIBAgIUJ8n...          ← 접힌 base64
-----END CERTIFICATE-----
```

(키 파일도 같은 모양이고 라벨만 다르다. **이 문서에 개인키 예시를 두지 않는다** —
`check-docs` 가 키 패턴을 잡고, 그 게이트가 옳다.)

**왜 [codec](codec.md) 만으론 안 되나.** 그쪽은 base64 를 알고 **줄 접기는 모른다**(그쪽 문서가
그렇게 적어 두었다). 이 모듈이 머리말을 벗기고 접힘을 펴서 넘긴다.

**왜 필요한가.** 목표가 공개 웹 HTTPS 서버가 되면서 생겼다. 인증서는 **밖에서 받는다** —
certbot 이 파일로 놓아주고, 우리는 그것을 읽기만 한다. **ACME 는 안 짓는다.**

## 표면

| op | 하는 일 |
|---|---|
| `find_from` | `hay` 안에서 `needle` 찾기 (없으면 `len hay`) |
| `body_off` | `-----BEGIN <라벨>-----` **다음 줄**의 자리. 0 = 없음 |
| `end_off` | `-----END <라벨>-----` 의 자리. 0 = 없음 |
| **`unwrap`** | **PEM 한 덩이 → DER 바이트.** 실패는 `none` |

`scratch` 는 두 몫을 진다 — 머리말 조립(앞)과 **펴 놓은 base64**(뒤).
⇒ `len scratch ≥ len src + 라벨 + 16` 이면 넉넉하다.

## ★ 라벨을 요구하는 이유

`unwrap` 은 **무엇을 여는지 이름으로** 받는다. 그리고 BEGIN 과 END 의 라벨이 다르면 **거절한다.**

한 파일에 여러 개가 들어 있는 것이 정상이다(인증서 **체인**). 짝을 안 맞추고 이어 붙이면
`CERTIFICATE` 를 열려다 다음 것의 BEGIN 까지 삼켜 **쓰레기를 디코드**한다 — 그리고 base64 는
쓰레기도 조용히 받아들인다.

## ★ 줄 끝은 LF 도 CRLF 도 받는다 — [http](http.md) 와 반대다

그쪽은 **경계가 곧 보안**이라 맨 LF 를 거절한다(요청 밀반입). 여기는 **파일 형식**이고
실제 파일이 둘 다 쓴다.

☞ 같은 저장소 안에서 규칙이 다른 이유를 적어 두지 않으면 다음 사람이 하나로 맞추려 든다.
  **엄격함은 미덕이 아니라 도구다** — 무엇을 막는지에 따라 세기가 달라진다.

## 개인키까지 — [der](der.md) 와 함께

```lowent
rem PEM → DER → PKCS#8 → 32 바이트 스칼라
let d option u64 . be pem.unwrap src "PRIVATE KEY" sc der_buf .
guard is_some d . else return 0 .
let dn u64 be some_value d .
let inner u64 be der.p8_inner_off (subslice der_buf 0 dn) .
let s u64 be der.ec_priv_off (subslice der_buf 0 dn) inner .
```

## ★ 왜 `decode` 가 아니라 `unwrap` 인가

`utf8.decode` 가 이미 있고, 이름공간이 **평면**이라 한정해서 불러도 도구가 **남의 시그니처로
타입을 잰다**(알려진 결함 — `docs/known-defects/qualifier-erased-flat-lookup.md`).
`check-lib-pairs` 가 `pem+regex`·`pem+term` 이 **조합되면 깨진다**고 잡았다.

☞ **라이브러리는 혼자 초록인 것으로 충분하지 않다 — 조합되어야 쓸 수 있다.**
  [gcm](gcm.md) 이 [aead](aead.md) 의 `seal`/`unseal` 을 피해 `encrypt`/`decrypt` 를 고른 것과 같은 자리다.

## 오라클

**진짜 openssl 산물**로 맞댄다: 인증서 DER **375 바이트**가 openssl 과 같고, PKCS#8 안의
스칼라가 **자리 36 · 길이 32**(`openssl asn1parse` 가 말한 그 자리)다.
그리고 라벨 불일치·봉투 없음을 거절한다. VM ≡ native · 태그 경로 op 0.

★ **벡터의 스칼라는 합성값 `01 02 … 20` 이다.** 처음엔 openssl 이 낸 진짜 키를 박았고
  `check-docs` 가 잡았다 — 게이트가 옳았다. 구조는 openssl 이 낸 그대로이므로 파서를 재는
  힘은 같고, **명백히 비밀이 아니므로 더 나은 벡터**다.
