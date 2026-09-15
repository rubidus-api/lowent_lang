#!/bin/sh
# 한국어판 PDF: 예제 전수 검증(출력 캡처) → 원고 점검 → typst → build/book.pdf
# typst 와 글꼴은 작업공간 공용 도구를 쓴다(TYPST · FONT_PATH 로 바꿀 수 있다).
# ★ 글꼴을 못 찾으면 조판은 성공하되 한글이 대체 글꼴로 떨어진다 — 그 PDF 는 버린다.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
ws=$(cd "$root/../../.." && pwd)
typst=${TYPST:-$ws/usr/toolchains/typst/typst}
fonts=${FONT_PATH:-$ws/toolchains/fonts}
[ -x "$typst" ] || { echo "build-book: typst 를 못 찾았다: $typst" >&2; exit 1; }

sh "$root/scripts/verify-examples.sh"
python3 "$root/scripts/check-headings.py"
python3 "$root/scripts/check-chapter-openings.py"
python3 "$root/scripts/check-floats.py"
python3 "$root/scripts/check-inline-code.py"

mkdir -p "$root/build"
log="$root/build/typst-ko.log"
"$typst" compile --root "$root" --font-path "$fonts" \
    "$root/book/main.typ" "$root/build/book.pdf" >"$log" 2>&1 || { cat "$log" >&2; exit 1; }
cat "$log"
if grep -q "unknown font family" "$log"; then
    echo "build-book: 글꼴을 찾지 못했다 ($fonts) — 대체 글꼴 PDF 는 버린다" >&2
    rm -f "$root/build/book.pdf"; exit 1
fi
echo "build-book: build/book.pdf"
