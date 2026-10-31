# 표준 명세

이 폴더는 **로우엔트 언어의 정의**다. 다른 문서 없이 이것만으로 언어를 배우고 구현할 수 있어야 한다.

| 무엇 | 파일 | 성격 |
|---|---|---|
| **조항 정본** | [`canon/*.md`](canon/) | ★ 규범. ISO 식 조항을 마크다운으로 적은 것 |
| 빌트인 표 | [`BUILTIN-MEANINGS.tsv`](BUILTIN-MEANINGS.tsv) | 내장 op 의 이름과 뜻 — 부록 D 가 이것을 읽는다 |
| 요약 | [`SPEC-BRIEF.md`](SPEC-BRIEF.md) | 정본에서 낸 한 장 요약(비규범) |
| 조판본 | `main.typ` · `lib.typ` · `clauses/*.typ` · `annex/*.typ` | 정본에서 낸 Typst 원고 |
| 웹 판 | [`html/index.html`](html/index.html) | 정본에서 낸 읽기용 판 |
| 판 | [`VERSION`](VERSION) | 명세의 판 |

`canon/*.md` 와 조판본·웹 판이 갈리면 **`canon/*.md` 가 이긴다** — 나머지는 그것에서 난 산출물이다.
