#!/bin/sh
# 매뉴얼 전부: Typst 원본(typst-ko · typst-en) → pdf-* · html-* · md-*
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
sh "$root/scripts/build-pdf.sh"
sh "$root/scripts/build-html.sh"
python3 "$root/scripts/build-md.py"
echo "build-all: pdf-ko pdf-en html-ko html-en md-ko md-en"
