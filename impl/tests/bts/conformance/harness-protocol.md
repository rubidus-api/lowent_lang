# 러너 규약 (Harness Protocol)

- 작성일: 2026-10-29
- 상태: 확정 (2026-10-29, 승인 게이트 7, 질문 H1~H6 모두 추천안). 게이트 16(2026-10-31)에서 다른 대상의 행과 `litmus`·`alloc_sequences`의 판정 단위를 적었다(C 기준 러너로 처음 써 본 결과). 게이트 9(2026-10-30)에서 `bts-results/2`(§5.1)와 세 묶음을 더했다.
- 근거: 요구사항 R6.3(외부 컴파일러 러너는 범위 밖, 규약만 제공), R6.7(대상 기술), 명세 [08](../spec/08-operators-contracts.md) §2.6~§2.7(남은 검사와 소프트웨어 연산 목록), 사용자 승인 사항(구현 강제 실행 모드, 2026-10-28)

이 규약은 언어 프로젝트(lowent, pinax 등)가 자기 처리기로 이 프로젝트의 시험 벡터를 돌리는 **러너**를 만들 때 지킬 입력, 판정, 출력을 정한다. 러너를 만드는 일은 각 언어 프로젝트의 몫이다. 이 프로젝트는 러너를 만들지 않는다.

## 1. 입력

- 시험 벡터: `vectors/*.json`(형식 `bts-vectors/1`, [README](README.md)).
- 대상 기술: 시험하는 대상의 `bts-target/1` 파일. 이 프로젝트의 모델 대상(`targets/`)이거나 처리기가 내는 것이다.
- 러너는 알지 못하는 형식의 큰 판(`bts-vectors/2` 등)을 받으면 시작하지 않는다.
- **빌드 모드**: 러너는 처리기를 **검사를 지우지 않는 빌드 모드**로 돌린다(빌드 모드가 지울 수 있는 `ovf` `shc` `nar`과 계약 검사가 모두 남는 모드, 08 §1.2·§2.4). 그 모드의 이름을 결과 머리의 `build_mode`에 적는다. 다른 모드로 돈 결과는 적합성 주장에 쓰지 않는다.
- **대상 값**: 처리기가 낸 대상 기술이 모델 대상과 다르면, 대상 값에 기대는 기대값(레이아웃, 원자 폭, 선택 타입의 유무)은 `python3 -I docs/conformance/gen.py --target <대상 기술>`이 그 기술로 다시 계산한 값을 쓴다. 이름이 같은 모델 대상의 열을 쓰는 것은 두 기술이 같을 때뿐이다.

## 2. 묶음별 판정

| 묶음 | 러너가 하는 일 | 합격 |
|---|---|---|
| `integer_ops` | `case.names`에서 그 언어의 이름을 찾아, 행마다 피연산자로 연산을 실행한다 | 결과가 기대값과 같다. `"trap"`이면 실행이 멈춘다. `"none"`이면 `chk`의 실패 값이다 |
| `float_ops` | 행의 16진 비트 패턴을 언어의 비트 재해석으로 `f32`(또는 `f64`) 값으로 만들고, 기본 모드(IEEE, 비정규수 지원, FTZ·축약·재결합 없음)로 연산한다. `sum_treeW`는 언어의 그 연산이다 | 결과의 비트 패턴이 같다. `"nan"`이면 어느 NaN이든 맞다(FC-01). `lt`·`eq`는 참·거짓이 같다 |
| `result_sets` | 장면을 `unsafe` 코드로 옮겨 여러 번 돌린다(경합 장면은 스레드 둘 이상). 관찰 값을 모은다 | 모든 관찰이 `allowed` 안에 있다. 허용 결과가 다 나오지 않아도 된다 |
| `alloc_sequences` | 처리기·표준 라이브러리의 아레나·풀(05 §3)로 단계를 차례로 한다. 오프셋은 영역 시작에서 잰다 | 기대 결과가 같고, 성공한 `allocate`마다 null 아님·정렬·영역 안·겹치지 않음이 성립한다. `array_sizes`는 언어의 크기 계산으로 본다. 그런 할당자를 두지 않는 처리기는 `language-policy`로 건너뛴다 |
| `litmus` | 시험을 원자 연산으로 옮겨 여러 번(권장 10만 번 이상) 돌리고, 결과(레지스터와 마지막 메모리)를 모은다 | 모든 결과가 `allowed` 안에 있다. 금지 결과가 한 번이라도 나오면 실패다. 허용 결과가 다 나오지 않아도 된다 |
| `programs` | 예제를 언어 문법으로 옮겨 번역한다(`target`이 있으면 그 대상으로). 옮길 수 없는 표기(언어에 없는 기능)는 `language-policy`로 건너뛴다 | `accept`이면 번역되고 `reject`이면 거부된다. 거부 까닭이 `rule`과 같은지는 판정하지 않는다 |
| `simd_ops` | 레인 연산은 행마다 벡터를 만들어 연산한다. `implementations: each`이면 구현마다 돌린다(§3). `kernels`는 `description`의 알고리즘을 언어의 가변 길이 벡터로 쓰고, 레인 수 1·4·16(그 대상에서 고를 수 있는 것)으로 돌린다. 가변 길이 벡터가 없는 처리기는 `kernels`를 `language-policy`로 건너뛴다 | 레인 값이 같다. 모든 구현과 모든 레인 수가 같은 답이다 |
| `casting_rules` | `relations`의 암시 변환 허용 여부와 `common_type`의 언어별 공통 타입은 **번역 결과**(받음·거부, 고른 타입)로, `narrowing`·`f32_to_int`·`int_to_f32`는 실행 값으로 본다 | 언어 열의 허용·거부, 공통 타입, 값이 같다 |
| `struct_layout` | 시험 대상의 열(또는 §1의 다시 계산한 값)을 쓴다. 언어의 크기·정렬·오프셋 질의나 처리기가 내는 C 머리글로 값을 읽는다. `bitfields`는 행마다 필드 값을 묶어 저장 단위 값과 메모리 바이트를 비교한다 | 값이 같다. `{unavailable}`·`{rejected}` 행은 번역이 거부되면 `pass`다 |
| `device_access` | 행마다 `device_ptr(space, addr)`와 그 폭의 `volatile_load`를 쓰는 작은 프로그램을 번역한다(처리기 대상 기술이 모델과 다르면 `gen.py`로 다시 만든 행) | `accept`이면 번역되고 `reject`이면 거부된다. `store_order`가 있으면 두 바이트 저장이 그 순서다(기계어로 확인) |
| `range_erasure` | 예제를 언어 문법으로 옮겨 번역하고, 처리기의 남은 검사 목록(08 §2.6)을 읽는다. `keep` 검사는 `fires_on` 입력으로 실행한다 | `remove` 검사가 목록에 없고, `keep` 검사가 목록에 있으며 그 입력에서 트랩한다. 검사를 하나도 지우지 않는 처리기와 증명으로만 지우는 처리기(바닥 의무가 없는 처리기, 08 §2.6)는 `remove` 판정을 `language-policy`로 건너뛰고 `keep`만 판정한다. 지운 검사가 옳은지는 `keep` 판정과 판 비교(§3)가 본다. 머리의 `declaration.erases_contracts`가 참인 실행(계약 검사를 지우는 빌드 모드, 08 §2.4)은 `erased`가 있는 검사를 그 `floor`·`fires_on`으로 판정한다. 지운 계약은 사실이 아니므로, 그 계약에 기대어 지워지던 검사가 남아 지운 계약을 어기는 입력에서 트랩해야 한다 |
| `fixed_ops` | 행의 저장 정수로 Q 형식 값을 만들고(언어의 비트 재해석 또는 저장 정수 생성자), 연산한다. 곱셈과 `convert`는 `rounding`이 고른 반올림으로 한다. 변환은 `to`·`type_b`가 고른 타입을 쓴다 | 결과의 저장 정수가 같다. `"trap"`·`"none"`은 `integer_ops`와 같다. Q 형식을 두지 않는 언어는 묶음 전체를 생략한다(§5.1) |
| `bigint_ops` | 행의 십진 문자열로 `bigint` 값을 만들고(언어의 문자열 변환 또는 고정 폭 정수의 조합) 연산한다. `narrow`는 `to` 타입으로 `disposition`이 고른 변환을 쓴다 | 결과 값이 같다. `"trap"`·`"none"`은 `integer_ops`와 같다. `bigint`을 두지 않는 언어(pinax, 05 장 §4)는 묶음 전체를 생략한다(§5.1) |
| `decimal_ops` | 행의 `[-]cEe` 문자열로 `decimal` 값을 만들고(계수와 지수를 따로 읽어도 된다) 케이스의 문맥(`precision`, `rounding`)으로 연산한다. `quantize`의 `e`는 정수, `from_integer`는 `type_b` 타입의 정수에서 바꾼다 | 결과가 **값으로** 같다(지수는 달라도 된다). `"trap"`은 `integer_ops`와 같다. `decimal`을 두지 않는 언어(pinax, 05 장 §4)는 묶음 전체를 생략한다(§5.1) |
| `canonical_encoding` | 행의 값을 그 타입으로 만들고 언어의 정규 인코딩 함수로 바이트를 얻는다. `usize`·`idiff`는 대상 폭과 같은 `width`만 | 바이트가 같다 |
| `build_modes` | 검사를 남긴 모드(§1)에서 `kept` 열을, 검사를 지우는 빌드 모드에서 `erased` 열을 본다. 지우는 모드로 돈 결과 파일은 그 모드가 지우는 검사를 머리의 `declaration.removes`에 적는다(§5.1). 그 모드가 지우지 않는 검사의 행은 `kept`로 판정한다 | 결과가 같다. `idx`·`div`·`tag`·`fcv`는 어느 모드에서도 트랩한다. 지우는 모드가 없는 언어는 `erased` 판정을 `language-policy`로 건너뛴다 |

- `"trap"` 행은 행마다 따로 실행한다(한 행의 트랩이 다른 행의 판정을 막지 않게).
- `integer_ops`에서 `width` 칸이 있는 케이스(`usize`·`idiff`)는 대상의 `usize` 폭과 같은 것만 돌리고, 나머지는 `target-unavailable`로 건너뛴다.
- `float_ops`에서 `status`가 `optional`인 케이스(`f64`)는 그 형식을 두지 않는 처리기가 `language-policy`로 건너뛴다. `float_ops`의 `names`는 비어 있다. 연산이 공유 핵심의 필수 연산이므로 아래 이름 규칙으로 건너뛰지 않고, 러너가 언어의 연산자에 대응시킨다.
- **다른 대상의 행** (게이트 16): 행 하나가 한 대상의 값인 곳(`struct_layout`의 `results{}`, `alloc_sequences`의 `array_sizes`, `integer_ops`·`canonical_encoding`의 `width`)에서 시험하는 대상의 것이 아닌 행은 `target-unavailable`로 건너뛴다.
- **행과 판정의 단위가 다른 묶음** (게이트 16): `litmus`의 행은 허용 결과마다 하나지만 판정은 시험마다 한다. 금지 결과가 한 번이라도 나오면 그 시험의 모든 행이 `fail`이고, 아니면 모두 `pass`다. `alloc_sequences`에서 기대값이 없는 단계(`deallocate`, `reset`, `fill`)는 그 단계가 끝나면 `pass`이고, `check_contents`는 바이트가 같으면 `pass`다.
- `integer_ops`에서 언어가 연산 이름을 갖지 않으면(`names`가 `null`) 그 행은 `language-policy`로 건너뛴다. `names`는 참고용이며, 언어는 같은 뜻의 다른 이름을 쓸 수 있다. 그때 러너가 대응을 적는다.

## 3. 구현 강제 실행 모드

처리기가 한 연산에 구현을 둘 이상 가지면(하드웨어 명령과 소프트웨어, 시작할 때 고르는 명령 집합의 판들), 러너는 **판마다** 같은 행을 돌린다. 결과 줄의 `impl`에 판의 이름을 적는다. 판들의 결과가 서로 다르면 실패다(보고서 G G3, G6).

## 4. 건너뜀

건너뜀의 까닭은 다음 셋뿐이다.

| 까닭 | 뜻 | 적합성 주장 |
|---|---|---|
| `language-policy` | 언어가 그 기능을 두지 않기로 했다(예: pinax의 `secret`, 계약) | 영향 없음 |
| `target-unavailable` | 대상 기술에 따르면 그 대상에 없는 타입을 쓰는 행(예: `rv32`에서 `i128`의 `integer_ops` 행), 또는 다른 대상의 행(§2, 게이트 16). `{unavailable}`로 표시된 레이아웃 행은 건너뜀이 아니라 번역 거부를 확인해 `pass`로 적는다 | 영향 없음. 단 번역 거부를 확인한 경우만 |
| `not-implemented` | 처리기가 아직 못 한다 | 그 묶음에 적합하다고 주장할 수 없다 |

## 5. 출력

결과는 JSON Lines 한 파일이다. 새 러너는 형식 `bts-results/2`(§5.1)를 쓴다. `bts-results/1` 파일은 그 판의 뜻 그대로 받는다. 새 판의 범위 보증으로 올리지 않는다.

- 첫 줄은 머리다: `{"schema": "bts-results/2", "runner", "language", "processor", "processor_version", "build_mode", "target", "vectors": {파일 이름: sha256}, "declaration"}`(`bts-results/1`에는 `declaration`이 없다)
- 그다음 행마다 한 줄: `{"suite", "case", "row", "status", "impl", "reason"}`
  - `row`(`bts-results/2`): 묶음 안의 **전역 행 번호**다. 0부터 $N-1$까지이고, 순서와 단위는 §5.2의 표가 정한다. `case`는 사람이 읽는 꼬리표다. `bts-results/1`의 `row`는 그 판의 뜻 그대로다
  - `status`: `pass` `fail` `skip` `error`(러너 자신의 오류)
  - `reason`: `fail`·`skip`·`error`일 때. `skip`이면 §4의 셋 가운데 하나
- 마지막 줄은 묶음별 집계다.
- **적합성 주장**: 어떤 묶음에서 `fail`·`error`·`not-implemented`가 없으면 그 처리기는 그 대상에서 그 묶음에 적합하다고 말할 수 있다. 주장에는 머리의 `vectors` 해시를 함께 적는다. `bts-results/2`에서는 §5.1의 조건이 더 붙는다.

### 5.1 선언과 한정된 주장 (`bts-results/2`, 게이트 9)

러너는 시험 **전에** 다음을 머리의 `declaration`에 적는다.

| 칸 | 뜻 |
|---|---|
| `suites` | 결과에 나오는 묶음마다 `supported` 또는 `omitted`(언어가 그 기능 전체를 두지 않음) |
| `implementations` | 구현 강제 실행 모드(§3)로 돌린 구현 판의 이름. 하나뿐이면 이름 하나 |
| `removes` | 이 빌드 모드가 증명 없이 지우는 검사(`ovf` `shc` `nar` 가운데, 08 §1.2). 적합성 실행은 빈 목록 |
| `erases_contracts` | 이 빌드 모드가 계약 검사를 지우는가(08 §2.4). 적지 않으면 거짓. 참이면 집계의 `claim` 끝에 `contracts:erased`가 붙는다 |
| `profile` | 언어의 프로파일 `system` 또는 `script`(01 §2.1, 2026-11-08). 적지 않으면 `system`이므로 칸이 없는 기존 파일의 뜻은 그대로다. `script`이면 프로파일이 덜어 준 타입과 기능의 행(예: `integer_ops`의 `usize`·`idiff`)을 `language-policy`로 건너뛰고, 집계의 `claim` 끝에 `profile:script`가 붙는다 |

- `omitted` 묶음의 행은 모두 `language-policy` 건너뜀이어야 한다. 집계의 `claim`은 `"not-applicable"`이다. 통과가 아니다.
- `supported`인데 실행한 행(`pass`·`fail`·`error`)이 하나도 없으면 주장하지 못한다.
- 벡터 파일에 없는 행 번호, 같은 구현으로 두 번 보고한 행은 오류다. 묶음의 $N$행이 모두(`pass` 또는 허용된 `skip`으로) 보고되지 않으면 주장하지 못한다(2026-10-30, M14).
- 행의 `impl`은 선언한 이름이어야 한다. 이름이 둘 이상이면 실행한 행마다 `impl`을 적는다.
- 구현 판이 둘 이상이면 **판마다** 실행한 행이 있고 $N$행이 모두 보고되어야 주장한다(§3). 이름이 하나면 `impl` 없는 행은 그 판의 행이다. 이름이 둘 이상일 때 `impl` 없는 건너뜀은 선언한 모든 판에 대한 보고로 센다. 같은 판 이름을 두 번 선언하지 않는다(2026-10-31, 게이트 17).
- `removes`가 비어 있지 않으면 `build_modes` 밖의 묶음은 주장하지 못한다.
- 주장할 수 있으면 집계의 `claim`은 묶음, 벡터 해시(앞 16자리), 대상, 빌드 모드, 구현 판을 적은 한 줄이다(`check_results.py`의 `claim`). 그 밖의 범위로 넓혀 말하지 않는다.
- 이 규칙의 한계: 리트머스를 여러 번 돌려 금지 결과가 안 나온 것은 그 결과가 불가능하다는 증명이 아니다. `ct` 판정은 §6대로 따로 한다.

### 5.2 행 번호 (2026-10-31)

결과 행의 `row`는 아래 표의 단위를 적힌 차례대로, 각 단위 안에서는 벡터 파일의 차례대로 센 번호다(0부터). 경로의 `[]`는 목록의 원소마다, `{}`는 객체의 값마다 내려간다. `x[].rows[]`에서 `rows`가 없는 원소는 한 행이다. 이 표가 정본이고, `gen.py`의 `ROW_UNITS`는 `scripts/check_docs.py`가 이 표와 비교하는 사본이다.

| 묶음 | 행 단위(차례대로) |
|---|---|
| `alloc_sequences` | `sequences[].steps[]`, `array_sizes[]` |
| `bigint_ops` | `cases[].rows[]` |
| `build_modes` | `checks[].rows[]` |
| `canonical_encoding` | `rows[]` |
| `casting_rules` | `relations[]`, `common_type[]`, `narrowing[].rows[]`, `f32_to_int[].rows[]`, `int_to_f32[].rows[]` |
| `decimal_ops` | `cases[].rows[]` |
| `device_access` | `rows[]` |
| `fixed_ops` | `cases[].rows[]` |
| `float_ops` | `cases[].rows[]` |
| `integer_ops` | `cases[].rows[]` |
| `litmus` | `tests[].allowed[]` |
| `programs` | `examples[]` |
| `range_erasure` | `examples[].checks[]` |
| `result_sets` | `scenarios[]` |
| `simd_ops` | `cases[].rows[]`, `kernels[]` |
| `struct_layout` | `structs[].results{}`, `builtins[].results{}`, `bitfields[].rows[]` |

## 6. 범위 밖

- 상수 시간(`ct`)의 기계어 판정(R5.4)은 이 규약이 아니라 보고서 F의 절차를 따른다.
- 러너의 구현 언어, 실행 방법, 병렬화.

## 7. 형식과 도구

- 결과 형식: [`schema/bts-results-2.schema.json`](schema/bts-results-2.schema.json)(이전 판 [`schema/bts-results-1.schema.json`](schema/bts-results-1.schema.json))
- 결과 파일 검사: `python3 -I docs/conformance/check_results.py 결과.jsonl`. 머리의 벡터 해시가 이 사본과 같은지, 행의 모양, 건너뜀 까닭, 집계와 적합성 주장, 그리고 `bts-results/2`의 선언과 한정된 주장(§5.1)을 확인한다. `check.py`가 이 도구를 스스로 시험한다.

## 8. 결정 (게이트 7)

| 번호 | 결정 |
|---|---|
| H1 | 결과는 JSON Lines `bts-results/1`, 머리에 벡터 파일 sha256 |
| H2 | 건너뜀의 까닭은 셋. `not-implemented`가 있으면 그 묶음의 적합 주장을 못 한다 |
| H3 | `"trap"` 행은 행마다 따로 실행한다 |
| H4 | `range_erasure`는 처리기의 남은 검사 목록으로 판정한다 |
| H5 | 구현이 둘 이상인 처리기는 구현 강제 실행 모드가 MUST |
| H6 | 결과 형식 스키마와 결과 파일 검사 도구를 이 프로젝트가 준다 |

게이트 7 검수(2026-10-29)에서 빌드 모드, 처리기 대상 기술의 기대값, `common_type`·`bitfields`, `{unavailable}` 기록법, `names`가 `null`인 행을 더했다.
