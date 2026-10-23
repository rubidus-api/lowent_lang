#import "../lib.typ": *

= `x509` --- X.509 인증서 읽기 <mod-x509>

#modhead(file: "lib/x509.low", layer: [L0 --- 순수 계산(호출자의 뒷받침)], caps: [없음])

X.509 인증서(DER)의 *뼈대를 읽어 준다*. 서명이 덮는 부분은 어디서 어디까지인지, 발급자와 주체는 누구인지, 공개키 · 유효기간 · 확장은 어디
있는지를 *자리(오프셋)와 길이*로 낸다. #modref("der")[`der`] 가 «공개키만 꺼낸다» 에서 멈춘 자리의 다음 칸이다.

#aside[읽기만 한다][
  이 모듈은 인증서를 *확인하지 않는다*. 서명이 맞는지, 체인이 이어지는지, 이름이 맞는지는 #modref("verify")[`verify`] 가 이 모듈 위에서 답한다.
  짓지 않은 것: 폐기 확인(CRL · OCSP) · 이름 제약 · 정책 · v1/v2 의 옛 모양 · UTF-8 이 아닌 이름 비교.
]

인증서의 모양과 이 모듈의 op 이 가리키는 자리는 이렇다.

```text
Certificate
├─ tbsCertificate ─────────────── tbs_off … tbs_end   ← 서명이 덮는 바이트
│   ├─ [0] version (없을 수도 있다)
│   ├─ serialNumber                tbs_field 0
│   ├─ signature                   tbs_field 1
│   ├─ issuer (발급자 이름)        issuer_off · elem_len
│   ├─ validity                    not_before · not_after
│   ├─ subject (주체 이름)         subject_off · elem_len
│   ├─ subjectPublicKeyInfo        spki_off
│   └─ [3] extensions              ext_value_off · is_ca · san_next
├─ signatureAlgorithm ─────────── sigalg_oid_off · sigalg_oid_len
└─ signatureValue ─────────────── sig_off · sig_len
```

#dtable(
  columns: 2,
  id: "mod-x509-ops",
  caption: [`x509` 의 op --- 자리를 내는 op 은 0 이면 «성하지 않다»],
  [*op*], [*하는 일*],
  [`tbs_off` · `tbs_end`], [서명이 덮는 바이트의 시작과 끝. 서명은 이 구간의 *원본 바이트* 위에서 확인한다],
  [`sigalg_oid_off` · `sigalg_oid_len`], [바깥 서명 알고리즘 OID 의 자리와 길이],
  [`sig_off` · `sig_len`], [서명 값(BIT STRING 의 «남은 비트 수» 바이트를 건너뛴 자리)],
  [`tbs_field`], [tbsCertificate 안의 `n` 번째 필드 자리(0 serial · 1 signature · 2 issuer · 3 validity · 4 subject · 5 공개키)],
  [`issuer_off` · `subject_off` · `elem_len`], [발급자 · 주체 이름 요소 *전체*의 자리와 길이 --- 이름은 바이트로 견준다],
  [`spki_off`], [공개키(SubjectPublicKeyInfo)의 자리],
  [`not_before` · `not_after`], [유효기간을 견줄 수 있는 한 수(`YYYYMMDDhhmmss`)로. 시간대는 `Z` 만 받는다],
  [`ext_value_off`], [OID 가 `2.5.29.<n>` 인 확장의 값 자리(17 = 주체 대체 이름 · 19 = basicConstraints)],
  [`is_ca`], [남을 발급할 수 있는 인증서인가. 확장이 *없으면 거짓*이다],
  [`san_next`], [주체 대체 이름의 DNS 이름을 하나씩 준다(`cur` 가 0 이면 첫째, 0 을 돌려주면 끝)],
)

*값을 베끼지 않는다.* 모든 op 이 자리와 길이만 낸다. 64 KiB 아레나에 인증서 몇 장을 한꺼번에 들어야 하고, 서명은 원본 바이트 위에서 확인해야
하기 때문이다 --- 베낀 자리에서 확인하면 베끼기가 틀려도 서명이 맞는 것처럼 보일 수 있다.

*길이는 전부 상대가 쓴 것이다.* 인증서는 아직 아무것도 믿을 수 없는 바이트열이다. 자리를 내는 길은 모두 `der.value_off` · `der.value_len`
을 지나고, 그 둘이 버퍼 밖을 막는다. 이 모듈은 그 위에 *차례*를 얹는다 --- X.509 의 필드에는 이름표가 없어서 몇 번째인지가 곧 무엇인지이고,
차례를 안 보면 남이 끼워 넣은 필드를 제 자리 것으로 읽는다.

*`is_ca` 의 기본값이 중요하다.* basicConstraints 확장이 없으면 CA 가 아니다(RFC 5280 §4.2.1.9). 이 기본값을 놓치면 잎 인증서가 중간 인증기관
노릇을 할 수 있다 --- 체인 검증에서 가장 흔한 구멍이다.
