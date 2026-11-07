#!/bin/sh
# PDF 두 판: 예제 전수 검증(출력 캡처) → 원고 점검 → typst → pdf-ko/ · pdf-en/
# typst 와 글꼴은 TYPST · FONT_PATH 로 준다(주지 않으면 작업공간 공용 도구 자리를 본다).
# ★ 글꼴을 못 찾으면 조판은 성공하되 한글이 대체 글꼴로 떨어진다 — 그 PDF 는 버린다.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
ws=$(cd "$root/../../.." && pwd)
typst=${TYPST:-$ws/usr/toolchains/typst/typst}
fonts=${FONT_PATH:-$ws/toolchains/fonts}
[ -x "$typst" ] || { echo "build-pdf: typst 를 못 찾았다: $typst" >&2; exit 1; }

sh "$root/scripts/verify-examples.sh"
python3 "$root/scripts/check-headings.py"
python3 "$root/scripts/check-chapter-openings.py"
python3 "$root/scripts/check-floats.py"
python3 "$root/scripts/check-inline-code.py"

mkdir -p "$root/build"
for lang in ko en; do
    out="$root/pdf-$lang/lowent-manual-$lang.pdf"
    log="$root/build/typst-$lang.log"
    mkdir -p "$root/pdf-$lang"
    "$typst" compile --input "lang=$lang" --root "$root" --font-path "$fonts" \
        "$root/typst-$lang/main.typ" "$out" >"$log" 2>&1 || { cat "$log" >&2; exit 1; }
    cat "$log"
    if grep -q "unknown font family" "$log"; then
        echo "build-pdf: 글꼴을 찾지 못했다 ($fonts) — 대체 글꼴 PDF 는 버린다" >&2
        rm -f "$out"; exit 1
    fi
    echo "build-pdf: pdf-$lang/lowent-manual-$lang.pdf"
done
