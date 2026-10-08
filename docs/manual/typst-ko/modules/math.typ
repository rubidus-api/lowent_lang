#import "../lib.typ": *

= `math` --- 부동소수 수학 <mod-math>

#modhead(file: "lib/math.low", layer: [L0 --- 순수 계산(호스트 전용)], caps: [없음])

`sqrt`·`sin`·`exp` 같은 기본 연산 위에 자주 쓰는 것들을 얹는다 --- 비교(`close`), 상수(`pi`·`e`), 각도 변환, 빗변, 밑이 있는 로그, 선형 보간. 권한은 없지만 C 수학
라이브러리에 붙으므로 *호스트 전용*이다 --- 운영체제 없는 대상에는 부동소수 자체가 없을 수 있다(`cortex_m` 은 `no_float`).

#aside[`==` 로 비교하지 않는다 --- `close` 를 쓴다][
  ```lowent
  guard math.close (math.hyp 3.0 4.0) 5.0 0.000001 else return 1 .
  ```
  부동소수에서는 "같다" 가 잘 정의되지 않는다(#chref("numbers")). 그래서 이 모듈은 `close(a, b, tol)` 을 주고, 라이브러리 자신의 시험도 그것으로 판정한다.
]

*확인이 비트가 아닌 까닭.* C 수학 라이브러리는 *구현마다 마지막 자리가 다를 수 있다*. 정확한 비트를 주장하면 다른 기계와 다른 libc 에서 도구가 멀쩡한데 시험이
실패하는 거짓 실패가 난다. 그래서 이 모듈의 확인은 *알려진 답*(`sin(0) = 0`)과 *항등식*(`sin²+cos² = 1`, `exp(log x) = x`)이고 판정은 `close` 로 한다. 얼마나
가까우면 같다고 볼지(`tol`)는 부르는 쪽의 문제이지 라이브러리가 정할 것이 아니다.

#dtable(
  columns: 2,
  id: "mod-math-ops",
  caption: [`math` 의 op],
  [*op*], [*하는 일*],
  [`close`], [`|a − b| ≤ tol` --- 이 모듈의 비교 방식],
  [`pi` · `e`], [상수],
  [`deg_to_rad` · `rad_to_deg`], [각도 변환],
  [`hyp`], [`sqrt(x² + y²)`],
  [`log_base`], [밑이 있는 로그(`log x / log b`)],
  [`lerp`], [선형 보간 `a + (b − a)·t`],
)

기본 연산으로 바로 쓰는 것 --- `sqrt`·`abs`·`floor`·`ceil`·`round`·`sin`·`cos`·`exp`·`log`·`pow`.

*짓지 않은 것* --- `atan2`·`asin`·`acos`·`tan`·`log2`·`log10`·`cbrt`, 복소수, 고정소수, `f32` 전용 얼굴. 필요해지면 기본 연산 하나와 이 파일 한 줄로 붙는다.
`hyp` 는 `sqrt(x²+y²)` 를 그대로 셈한다 --- libm 의 `hypot` 처럼 넘침을 피하는 재조정을 하지 않으므로 아주 큰 값에서는 다르게 답할 수 있다.
