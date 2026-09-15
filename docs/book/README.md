# Lowent Book

Lowent 를 처음부터 끝까지 설명하는 책이다. 한국어판(원본)과 영어판이 같은 장 차례와 같은 예제를 나누어 쓴다.
A book that explains Lowent from start to finish. The Korean edition (original) and the English edition share one chapter order and one set of examples.

| | 한국어 · Korean | English |
|---|---|---|
| 원고 · manuscript | [`book/`](book/) | [`book-en/`](book-en/) |
| PDF | `scripts/build-book.sh` → `build/book.pdf` | `scripts/build-book-en.sh` → `build/book-en.pdf` |
| 웹 · web | `scripts/build-html.sh` → `web/ko/` | `scripts/build-html.sh` → `web/en/` |

10부 43장과 부록 넷(낱말·진단·흔한 실수·문법)으로 되어 있다. 제9부는 표준 라이브러리를, 제10부는 증명과 그 한계를 다룬다.
Ten parts, 43 chapters and four appendices (vocabulary, diagnostics, common mistakes, grammar). Part IX tours the standard library; Part X covers the proofs and their limits.

## 예제는 실제로 돌린 것이다 · Every example is run

[`examples/`](examples/) 의 `.low` 파일마다 첫머리에 무엇을 해야 하는지 적혀 있다(`rem run:` · `rem trap:` · `rem expect:` · `rem test` …).
[`scripts/verify-examples.sh`](scripts/verify-examples.sh) 가 파일마다 그 약속을 확인하고 — 실행되는 예제는 VM 과 네이티브 빌드가 같은 답을 내는지까지 —
출력을 `build/examples-out/` 에 남긴다. 책은 그 출력을 그대로 싣는다.

Each `.low` file under `examples/` states at its top what must happen. `scripts/verify-examples.sh` checks it — for running examples, that the VM and a
native build agree — and the book prints the captured output as is.

## 짓기 · Building

- `lowentc`(저장소의 `impl/` 에서 `make`), C 컴파일러, [Typst](https://typst.app) 0.15 이상, Noto Sans·Noto Serif·Noto Sans Mono·Noto Sans/Serif CJK KR·D2Coding 글꼴이 필요하다.
  Requires `lowentc` (`make` in `impl/`), a C compiler, Typst 0.15+, and the Noto Sans, Noto Serif, Noto Sans Mono, Noto Sans/Serif CJK KR and D2Coding fonts.
- `TYPST` 와 `FONT_PATH` 환경 변수로 typst 실행 파일과 글꼴 폴더를 지정한다. · Set `TYPST` and `FONT_PATH` to point at typst and the font folder.
- 글꼴을 찾지 못하면 빌드 스크립트가 그 PDF 를 버린다. · If fonts are missing, the build script discards the PDF.

## 라이선스 · License

본문은 CC BY-NC-SA 4.0, 예제와 스크립트는 MIT 다. [`LICENSE-NOTICE.md`](LICENSE-NOTICE.md) 를 본다.
Text under CC BY-NC-SA 4.0; examples and scripts under MIT. See [`LICENSE-NOTICE.md`](LICENSE-NOTICE.md).
