#import "../lib.typ": *

= `bigint` --- 큰수 모듈러 산술(기계가 고르는 폭) <mod-bigint>

#modhead(file: "lib/bigint.low", layer: [L0 --- 순수 계산(호출자의 뒷받침)], caps: [없음])

#modref("bigint32")[`bigint32`] 와 op 의 이름 · 입력 · 뜻이 모두 같다. 다른 것은 *조각 폭을 기계가 고른다*는 것 하나다 --- 대상의
*곱셈기 폭*(`machine.mul_bits`)이 16 이면 #modref("bigint16")[`bigint16`], 32 이면 `bigint32`, 그 밖이면 #modref("bigint64")[`bigint64`] 의 셈을 부른다.
표준 라이브러리의 #modref("rsa")[`rsa`] · #modref("p256")[`p256`] · #modref("p384")[`p384`] · #modref("ecdsa")[`ecdsa`] · #modref("ed25519")[`ed25519`] · #modref("verify")[`verify`] 가 이것을 쓴다.

#dtable(
  columns: 3,
  id: "mod-bigint-mulbits",
  caption: [대상마다 곱셈기 폭 --- `machine.mul_bits` 의 값],
  [*대상*], [*곱셈기 폭*], [*고르는 모듈*],
  [`x86_64` · `arm64` · `riscv64` · `win64`], [64], [`bigint64`],
  [`cortex_m` · `mips_be` · `cheriot`], [32], [`bigint32`],
  [`word16`], [16], [`bigint16`],
)

*프로그램 전체의 폭을 바꾸려면* `lowentc --mul-bits 16 …` (또는 `32` · `64`) --- `machine.mul_bits` 의 값이 바뀌고, `bigint` 를 쓰는 라이브러리가
모두 그 폭으로 돈다. *한 자리에서만 다른 폭*을 쓰려면 폭을 받는 op(`rsa.verify_pss_by` …)에 폭 타입을 넘긴다(#modref("bigwidth")[`bigwidth`]).

#aside[곱셈기 폭은 빠르기의 기본값이다][
  어느 폭이든 어느 기계에서나 *같은 답*을 낸다 --- 값은 어차피 `u64` 칸에 든다. 그리고 이 기준이 늘 가장 빠른 폭을 고르는 것도 아니다: x86 32 비트
  빌드(`-m32`)는 곱셈기가 32 비트인데 `bigint64` 가 `bigint32` 보다 1.35 배 빨랐다(RSA-2048 거듭제곱, 2026-10-22). Cortex-M0 · M0+ 는 곱의 위 절반을
  내는 명령이 없다고 알려져 있어 16 이 맞을 수 있지만, `cortex_m` 한 줄이 M3 · M4 와 함께 덮는다(확인하지 않았다).
]

폭이 *번역 때* 정해지므로 고르는 비용이 없다 --- 방출된 C 에는 그 값이 글자 그대로 들어가 `cc` 가 비교를 접는다. 담는 칸은 폭과 상관없이
⌈`nb` / 2⌉ + 2 칸이면 된다(`bigint16` 이 가장 많이 쓴다).

#dtable(
  columns: 3,
  id: "mod-bigint-newops",
  caption: [2026-10-23 에 넷 모두에 더한 op --- 곡선 모듈이 스스로 하던 조각 셈],
  [*op*], [*하는 일*], [*요구 · 비고*],
  [`add_mod` · `sub_mod`], [`o ← (a ± b) mod m`], [`a`, `b` 는 `m` 보다 작다 · `o` 는 `a` 나 `b` 와 같은 칸이어도 된다],
  [`is_zero`], [`nb` 바이트짜리 수가 0 인가(0 · 1)], [],
  [`equal`], [두 수가 같은가(0 · 1)], [이름이 `eq` 가 아닌 것은 그것이 빌트인이기 때문이다],
  [`bit`], [수의 `i` 번째 비트(0 이 가장 낮다)], [지수 · 스칼라를 위에서부터 읽을 때],
)

*확인하는 것* --- 네 모듈(`bigint16` · `bigint32` · `bigint64` · `bigint`)의 새 op 이 파이썬 정수 산술과 같은지(17 · 32 · 33 · 48 바이트), 곡선 서명
검증과 RSA 검증이 세 폭(`--mul-bits 16 · 32 · 64`) 모두에서 같은 답을 내는지. 다섯 파일(`bigint16/32/64` · `bigwidth` · `bigint`)은 한 틀
(`scripts/gen-bigint.py`)에서 나온다 --- 한 파일만 고치면 틀과의 대조가 실패한다.
