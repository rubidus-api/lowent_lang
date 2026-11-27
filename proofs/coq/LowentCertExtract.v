(* LowentCertExtract.v — **판정자를 OCaml 로 추출한다.** RFC-0086 §6-2 의 마지막 줄.
 *
 * `LowentCert.v` 는 규칙의 건전성을 증명하고 `check_cert` 를 준다. `check-cert-coq.sh` 는
 * 그 함수를 **Coq 안에서** 돌린다(맞지만 느리다 — 매번 파일을 만들고 컴파일한다).
 *
 * 이 파일은 그 함수를 **OCaml 로 추출**한다. 그러면 판정자가 **독립 실행 파일**이 되고,
 * 그 코드는 *사람이 쓴 것* 이 아니라 **증명에서 파생된 것**이다.
 *
 * ★ 전에는 못 했다. 그 이유를 `LowentCert.v` §4 에 이렇게 적었다:
 *     "이 환경에 zarith 의 **인터페이스(.cmi)가 없다** ⇒ Z 를 63비트 int 로 매핑하면
 *      2^63−1 이 **조용히 깨지고**, Int64 브리지를 손으로 쓰면 **없애려던 신뢰 대상**을
 *      되살린다."
 *   그런데 **Rocq 9 를 짓느라 zarith 를 소스에서 빌드했다**(2026-07-31) — 인터페이스까지.
 *   ⇒ 막고 있던 것이 사라졌다. **다른 일을 하다가 조건이 갖춰지는** 일이 있다.
 *
 * 임의 정밀도(Z → Big_int_Z)로 추출하므로 폭 문제가 없다.
 *)

Require Import ZArith List Bool.
Import ListNotations.
Require Import LowentCert.

Require Import Coq.extraction.Extraction.
Extraction Language OCaml.
Require Import ExtrOcamlBasic ExtrOcamlZBigInt.

Set Extraction Output Directory ".".
Extraction "lowent_cert.ml" check_cert.
