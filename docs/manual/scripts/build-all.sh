#!/bin/sh
# 매뉴얼 전부: Typst 원본(typst-ko · typst-en) → pdf-* · html-* · md-*
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
# ★ 부록 B(진단 찾아보기)는 예제에서 짓는다 — 먼저 다시 지어야 예제를 고친 판에서 부록이 낡지 않는다(2026-10-31)
python3 "$root/scripts/gen-diag-appendix.py"
sh "$root/scripts/build-pdf.sh"
sh "$root/scripts/build-html.sh"
python3 "$root/scripts/build-md.py"
echo "build-all: pdf-ko pdf-en html-ko html-en md-ko md-en"
