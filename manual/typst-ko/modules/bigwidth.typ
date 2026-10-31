#import "../lib.typ": *

= `bigwidth` --- 큰 수 폭의 트레이트 <mod-bigwidth>

#modhead(file: "lib/bigwidth.low", layer: [L0 --- 순수 계산], caps: [없음])

트레이트 `width` 하나만 든다. #modref("bigint16")[`bigint16`] · #modref("bigint32")[`bigint32`] · #modref("bigint64")[`bigint64`] · #modref("bigint")[`bigint`] 가
저마다 이것을 갖춘 타입 `limb` 을 낸다 --- `bigint16.limb` 은 «16 비트 조각으로 셈한다» 는 뜻의 값이다. 큰 수를 쓰는 라이브러리 op 이 이
트레이트를 받으면, 부르는 쪽이 *부를 때마다* 폭을 고른다. 이것을 받는 op 은 #modref("rsa")[`rsa`] 의 `verify_pss_by` · `verify_pkcs1_by`, 그리고 2026-10-24 부터 큰 수를 셈하는 곡선 op 마다의
`_by` 짝이다(#modref("p256")[`p256`] · #modref("p384")[`p384`] · #modref("ed25519")[`ed25519`] · #modref("ecdsa")[`ecdsa`] --- 손으로 쓴 op 에서 `scripts/gen-curve-by.py` 가 낸다).

```text
proc verify_pss_by input comptime wt type . input lw wt . … requires bigwidth.width wt. . .
  … method lw. mod_exp. ow. sw. e. nw. mb. r2. acc. mw. tw. . …

let w lit bigint16.limb do tag 0 . end . .
rsa.verify_pss_by bigint16.limb. w. sig. n. …     ← 이 한 번만 16 비트 조각으로
```

메서드는 큰 수 모듈의 op 과 이름 · 입력이 같다 --- `limbs` · `zero` · `from_bytes` · `to_bytes` · `from_bytes_le` · `to_bytes_le` · `ge_mod` · `dbl_mod` ·
`mont_mul` · `n0inv` · `mod_exp` · `add_mod` · `sub_mod` · `is_zero` · `equal` · `bit`. 폭이 번역 때 정해지므로 고르는 비용이 없다(RSA-2048
거듭제곱 300 번 · 네이티브 `-O2` · x86-64: 직접 부름 0.101 초, 트레이트로 0.102 초 --- 2026-10-23 잼).

*왜 따로 둔 모듈인가* --- 폭 모듈 넷이 이것을 쓰고, `bigint` 가 폭 모듈 셋을 쓴다. 트레이트가 그 가운데 하나에 있으면 서로를 부르는 고리가 생긴다.

*프로그램 전체*의 폭을 바꾸는 길은 따로 있다 --- `pkg.low` 의 `build limb_bits` 나 `lowentc --limb-bits` (#modref("bigint")[`bigint`]).
