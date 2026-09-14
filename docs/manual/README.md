# Lowent 매뉴얼 (초본)

> 이것은 **입문 초본**이다. 언어가 본격적으로 완성됨에 맞춰 이어 써 나간다.
> 규범은 **조항 정본** [`docs/spec/canon/`](../spec/canon/) 이며,
> 이 문서와 어긋나면 규범이 이긴다.
> 아래 모든 예제는 현재 `lowentc` 로 확인한 것이다 — 정답 예제는 `--check` 초록,
> 반례(✗)는 적힌 `E-…` 에러를 실제로 낸다.

## 목차

1. [빌드와 실행](01-build-and-run.md) — `lowentc`, 모드, `hello, entropy`
2. [문법의 기본](02-syntax-basics.md) — 토큰·주석·상수·스칼라 타입·키워드
3. [`fn`/`proc` — 프로그램의 기본 단위](03-ops.md) — `fn`/`proc`·매개변수·계약·`cap`·`effect`
4. [지역 변수](04-variables.md) — `let`·`var`·`set`
5. [배열과 슬라이스](05-arrays-and-slices.md) — `array`·`slice`·`index`·`for`·`mut`
6. [참조](06-references.md) — `ref`·`mut_ref`·빌림 규칙
7. [`guard` 와 없을 수 있는 값](07-guard-and-option.md) — `guard`·`option`·`some_value`
8. [나만의 타입](08-types.md) — `struct`·페이로드 `enum`·`range`·`trait`(op 여럿)
9. [`match` — 경우 나누기](09-match.md) — 패턴·와일드카드·범위·or·바인딩·`option`/`result`·가드
10. [실수 치트시트](10-mistakes-cheatsheet.md) — 흔한 실수 ↔ 에러 ↔ 고치는 법
11. [표준 라이브러리](lib/README.md) — `lib/` 의 표준 모듈, 모듈마다 한 편
12. [**이론 배경과 증명**](theory/README.md) — 이 언어가 **무엇을 증명했나** · 16장
    · 필요한 수학은 [02장](theory/02-math-toolkit.md)에서 처음부터 가르친다(고등학교 수학이면 된다)
    · **소스 코드를 몰라도 읽힌다** — 언어 사용법이 아니라 그 근거를 다룬다
    · 마지막 장은 [**증명하지 않은 것**](theory/16-what-is-not-proven.md)이다. 그 장을 빼고
      인용하면 이 문서가 말하지 않은 것을 말한 것으로 만든다

---

<sub>초본 · §1–§5 초안 + §6–§10 추가(2026-07-24) + §9 match(2026-07-25). 예제는 `lowentc` 로 실측 확인.
언어 완성과 함께 계속 이어쓴다.
§12 이론 배경 매뉴얼 16장 추가(2026-07-30, RFC-0085).</sub>

> 본문에 나오는 RFC 번호·증명 파일(`*.v`)·검사 스크립트(`scripts/…`)·장부(`UNPROVEN.md` 등)는 **개발 저장소**의 것이라
> 이 공개본에는 들어 있지 않다. 근거를 밝히려고 이름을 남겨 두었다.
