#!/bin/sh
# 초고 작업용: 점검 없이 한국어판만 조판한다(원고 점검은 build-book.sh 가 한다).
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
ws=$(cd "$root/../../.." && pwd)
typst=${TYPST:-$ws/usr/toolchains/typst/typst}
fonts=${FONT_PATH:-$ws/toolchains/fonts}
lang=${1:-ko}
main=$root/book/main.typ; [ "$lang" = en ] && main=$root/book-en/main.typ
mkdir -p "$root/build"
"$typst" compile --root "$root" --font-path "$fonts" "$main" "$root/build/quick-$lang.pdf" 2>&1 | grep -v '^$' | head -30
