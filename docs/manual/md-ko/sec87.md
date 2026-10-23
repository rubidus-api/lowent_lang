# <a id="mod-bigwidth"></a>`bigwidth` — 큰 수 폭의 트레이트

소스

`lib/bigwidth.low`

층

L0 — 순수 계산

권한

없음

트레이트 `width` 하나만 든다. [`bigint16`](sec88.md#mod-bigint16) · [`bigint32`](sec89.md#mod-bigint32) · [`bigint64`](sec90.md#mod-bigint64) · [`bigint`](sec91.md#mod-bigint) 가 저마다 이것을 갖춘 타입 `limb` 을 낸다 — `bigint16.limb` 은 «16 비트 조각으로 셈한다» 는 뜻의 값이다. 큰 수를 쓰는 라이브러리 op 이 이 트레이트를 받으면, 부르는 쪽이 **부를 때마다** 폭을 고른다.

```text
proc verify_pss_by input comptime wt type . input lw wt . … requires bigwidth.width wt. . .
  … method lw. mod_exp. ow. sw. e. nw. mb. r2. acc. mw. tw. . …

let w lit bigint16.limb do tag 0 . end . .
rsa.verify_pss_by bigint16.limb. w. sig. n. …     ← 이 한 번만 16 비트 조각으로
```

메서드는 큰 수 모듈의 op 과 이름 · 입력이 같다 — `limbs` · `zero` · `from_bytes` · `to_bytes` · `from_bytes_le` · `to_bytes_le` · `ge_mod` · `dbl_mod` · `mont_mul` · `n0inv` · `mod_exp` · `add_mod` · `sub_mod` · `is_zero` · `equal` · `bit`. 폭이 번역 때 정해지므로 고르는 비용이 없다(RSA-2048 거듭제곱 300 번 · 네이티브 `-O2` · x86-64: 직접 부름 0.101 초, 트레이트로 0.102 초 — 2026-10-23 잼).

**왜 따로 둔 모듈인가** — 폭 모듈 넷이 이것을 쓰고, `bigint` 가 폭 모듈 셋을 쓴다. 트레이트가 그 가운데 하나에 있으면 서로를 부르는 고리가 생긴다.

**프로그램 전체**의 폭을 바꾸는 길은 따로 있다 — `lowentc --mul-bits` ([`bigint`](sec91.md#mod-bigint)).

---

[← 이전](sec86.md) · [목차로](README.md) · [다음 →](sec88.md)
