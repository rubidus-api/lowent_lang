# 10. 실수 치트시트 — 실수하면 컴파일러가 뭐라고 하나

[← 목차](README.md) · [← 9. match](09-match.md)

| 실수 | 에러 | 고치는 법 |
|---|---|---|
| `let` 에 `set` | `E-IMMUTABLE` | `var` 로 선언 |
| `mut` 아닌 슬라이스에 쓰기 | `E-TYPE-MUT` | `mut slice` 로 받기 |
| `ref` 로 쓰기 | `E-TYPE-REF` | `mut_ref` 로 |
| `ref` 를 `mut` 자리에 | `E-TYPE-ARGMUT` | 진짜 `mut` 를 넘기기 |
| `for x in xs` | `E-VOCAB-REMOVED` | `for x xs` (`in` 없음) |
| `guard` 의 `else` 안 떠남 | `E-GUARD-FALLTHROUGH` | `else` 에서 `return`/`break`, 아니면 `if` |
| 순수 `fn` 에 `effects io` | `E-EFFECT-CALC` | `proc` 으로 |
| 범위 밖 `index` | `E-VM-BOUNDS`(실행 중) | `guard` 로 `len` 먼저 확인 |
| enum 변형이 자기 타입을 값으로 | `E-ENUM-INFINITE` | 인덱스(`u32`)나 `owned` 로 |
| `get` 의 변형·필드 오타 | `E-ENUM-NOVARIANT` / `E-ENUM-NOFIELD` | 선언한 이름으로 |
| `match` 에서 경우 빠뜨림 | `E-MATCH-INEXHAUSTIVE` | 빠진 `case` 또는 `case _ .` 추가 |
| 같은 경우 두 번 / `_` 뒤 팔 | `E-MATCH-REDUNDANT` | 죽은 팔 지우기 |
| `case` 바인딩 개수 안 맞음 | `E-MATCH-ARITY` | 필드마다 이름 하나씩 |

⟹ Lowent 의 규율은 **"틀린 코드는 컴파일러가 이유를 대며 막는다"** 이다. 에러 코드(`E-…`)는
**안정적**이라, 도구·AI 가 그것만 보고도 무엇이 틀렸는지 안다(`--diag-json` 으로 기계가 읽는다).

---

[← 목차](README.md)
