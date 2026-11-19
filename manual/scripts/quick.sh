#!/bin/sh
# 초고 작업용: 점검 없이 한 판만 조판한다(원고 점검은 build-pdf.sh 가 한다).
#   scripts/quick.sh [ko|en]  → build/quick-<판>.pdf
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
ws=$(cd "$root/../../.." && pwd)
typst=${TYPST:-$ws/usr/toolchains/typst/typst}
fonts=${FONT_PATH:-$ws/toolchains/fonts}
lang=${1:-ko}
mkdir -p "$root/build"
# 원고의 #demo 는 예제 실행 출력(build/examples-out)을 싣는다 --- 새로 받은 복제본에는 없으므로 먼저 만든다.
[ -d "$root/build/examples-out" ] || sh "$root/scripts/verify-examples.sh" >/dev/null
"$typst" compile --root "$root" --font-path "$fonts" "$root/typst-$lang/main.typ" "$root/build/quick-$lang.pdf" 2>&1 | grep -v '^$' | head -30
