# Lowent 매뉴얼 · Lowent Manual

Lowent 를 처음부터 끝까지 설명하는 책 형식의 매뉴얼이다. 언어 편, 표준 라이브러리 편(제9부와 부록 E 의 모듈 쪽), 근거 편(제10부 --- 무엇이
증명되었고 무엇이 아닌가)이 한 권에 들어 있다. 한국어판(원본)과 영어판이 같은 장 차례와 같은 예제를 나누어 쓴다.

A book-form manual that explains Lowent from start to finish: the language, the standard library (Part IX and the per-module pages of Appendix E) and the
grounds (Part X --- what has been proven and what has not), in one volume. The Korean edition (original) and the English edition share one chapter order and
one set of examples.

## 폴더 · Layout

| 폴더 · folder | 무엇 · what |
|---|---|
| [`typst-ko/`](typst-ko/) · [`typst-en/`](typst-en/) | 원고(Typst) --- 이것이 원본이다 · the manuscript, the single source |
| `pdf-ko/` · `pdf-en/` | PDF 판 — 저장소에는 없고 짓고 나서 Pages 에 올린다 · PDF editions — built locally, published on Pages, not kept in the repository |
| [`html-ko/`](html-ko/) · [`html-en/`](html-en/) | 웹 판 · web editions |
| [`md-ko/`](md-ko/) · [`md-en/`](md-en/) | Markdown 판(GitHub 에서 바로 읽는다) · Markdown editions for reading on GitHub |
| [`examples/`](examples/) | 책에 실린 `.low` 예제 · the examples printed in the book |
| [`scripts/`](scripts/) · [`styles/`](styles/) | 짓기 도구와 조판 모양 · build tools and styles |

`pdf-*` · `html-*` · `md-*` 는 원고에서 만든 **생성물**이다(PDF 는 커밋하지 않는다). 고칠 때는 `typst-*` 를 고치고 다시 짓는다.
`pdf-*`, `html-*` and `md-*` are generated from the manuscript. Edit `typst-*` and rebuild.

## 예제는 실제로 돌린 것이다 · Every example is run

[`examples/`](examples/) 의 `.low` 파일마다 첫머리에 무엇을 해야 하는지 적혀 있다(`rem run:` · `rem trap:` · `rem expect:` · `rem test` …).
[`scripts/verify-examples.sh`](scripts/verify-examples.sh) 가 파일마다 그 약속을 확인하고 --- 실행되는 예제는 VM 과 네이티브 빌드가 같은 답을 내는지까지 ---
출력을 `build/examples-out/` 에 남긴다. 책은 그 출력을 그대로 싣는다.

Each `.low` file under `examples/` states at its top what must happen. `scripts/verify-examples.sh` checks it --- for running examples, that the VM and a
native build agree --- and the book prints the captured output as is.

## 짓기 · Building

```sh
sh scripts/build-all.sh     # 예제 검증 → PDF 두 판 → 웹 두 판 → Markdown 두 판
sh scripts/quick.sh ko      # 원고만 빠르게 조판해 오류를 본다 · quick compile check
```

- `lowentc`(저장소의 `impl/` 에서 `make`), C 컴파일러, [Typst](https://typst.app) 0.15 이상, Noto Sans · Noto Serif · Noto Sans Mono ·
  Noto Sans/Serif CJK KR · D2Coding 글꼴이 필요하다. 웹 글꼴 줄이기에는 `fonttools` 가 든 파이썬 환경(`WEBFONT_VENV`)을 쓴다.
  Requires `lowentc` (`make` in `impl/`), a C compiler, Typst 0.15+, the fonts above, and a Python environment with `fonttools` for web font subsetting.
- `TYPST` 와 `FONT_PATH` 환경 변수로 typst 실행 파일과 글꼴 폴더를 지정한다. · Set `TYPST` and `FONT_PATH`.
- 글꼴을 찾지 못하면 빌드 스크립트가 그 PDF 를 버린다. · If fonts are missing, the build script discards the PDF.

## 발행 · Publishing

- **웹 · GitHub Pages**: `gh-pages` 가지를 내보낸다. 그 가지는 main 의 `docs/` 한 벌에 PDF 셋을 더한 **커밋 하나**이고, 낼 때마다 갈아 끼운다 —
  PDF(한 벌 약 14 MB)가 main 의 기록에 쌓이지 않게 하려는 것이다(2026-10-03). 원고를 고치면 `build-all.sh` 로 짓고 html·md 를 커밋·푸시한 뒤
  Pages 가지를 다시 낸다. Pages serves the `gh-pages` branch: one commit holding `docs/` plus the three PDFs, replaced on every publish so the
  PDFs do not pile up in the history of `main`. Rebuild, commit and push the html/md, then republish the Pages branch.

## 라이선스 · License

본문은 CC BY-NC-SA 4.0, 예제와 스크립트는 MIT 다. [`LICENSE-NOTICE.md`](LICENSE-NOTICE.md) 를 본다.
Text under CC BY-NC-SA 4.0; examples and scripts under MIT. See [`LICENSE-NOTICE.md`](LICENSE-NOTICE.md).
