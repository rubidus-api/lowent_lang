#!/bin/sh
# 수록 예제 전수 검증. 책의 #demo 가 싣는 출력은 여기서만 만들어진다(손으로 옮겨 적지 않는다).
#
# examples/<장>/<이름>.low 마다 파일 안의 지시를 따른다:
#   rem run: <op> [인자…]     VM(--run)으로 돌리고, 네이티브(--emit-c → cc)로도 돌려 **출력이 같아야** 한다.
#                              여러 줄 적을 수 있다.
#   rem expect: <진단 코드>   --check 가 그 코드로 **거절해야** 한다(반례).
#   rem trap: <op> [인자…]    VM 과 네이티브가 **둘 다 멈춰야**(비영 종료) 한다. VM 의 진단을 싣는다.
#   rem flags: <깃발…>        검사할 때 lowentc 에 덧붙인다(예: --target cortex_m). 캡처의 명령 줄에도 싣는다.
#   rem ir                    --ir 가 내는 «runtime checks» 두 줄(지운 검사의 수)을 캡처 끝에 싣는다.
#   rem fmt                   --fmt 이 내는 정규형을 캡처 끝에 싣는다.
#   rem db                    --emit-db 가 내는 op 의 해시 줄(iface·def)을 캡처에 싣는다.
#   rem test-fail             --test 가 실패해야 한다(실패하는 시험을 보이는 예제). 캡처에 싣는다.
#   rem test                  --test 로 `test` 블록을 돌려 전부 통과해야 한다(캡처에 싣는다).
#   (지시가 없으면)            --check 가 통과해야 한다.
# 캡처: build/examples-out/<장>/<이름>.low.out — 명령 줄(`$ lowentc …`)과 출력.
# 하나라도 실패하면 비영 종료한다.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
lowentc=${LOWENTC:-$root/../../impl/build/lowentc}
cc=${CC:-cc}
[ -x "$lowentc" ] || { echo "verify-examples: lowentc 가 없다 ($lowentc) — impl 에서 make" >&2; exit 2; }
outdir="$root/build/examples-out"
bindir="$root/build/examples-bin"
mkdir -p "$outdir" "$bindir"
total=0 fail=0

for src in $(find "$root/examples" -name '*.low' | sort); do
  total=$((total + 1))
  rel=${src#"$root/examples/"}
  dir=$(dirname "$src"); name=$(basename "$src")
  out="$outdir/$rel.out"; mkdir -p "$(dirname "$out")"
  expect=$(sed -n 's/^rem expect: *\([A-Z0-9-]*\).*/\1/p' "$src" | head -n 1)
  runs=$(sed -n 's/^rem run: *//p' "$src")
  traps=$(sed -n 's/^rem trap: *//p' "$src")
  flags=$(sed -n 's/^rem flags: *//p' "$src" | head -n 1)

  if [ -n "$expect" ]; then
    # shellcheck disable=SC2086
    res=$(cd "$dir" && "$lowentc" --check $flags "$name" </dev/null 2>&1)
    if ! printf '%s\n' "$res" | grep -q "$expect"; then
      echo "  [FAIL] $rel — $expect 로 거절되지 않았다"; printf '%s\n' "$res" | head -5; fail=$((fail + 1)); continue
    fi
    { echo "\$ lowentc --check ${flags:+$flags }$name"
      printf '%s\n' "$res" | grep -E '^ *([^ ]*:)?[0-9]+:[0-9]+ [EW]-' | head -n 3 | sed 's/^ *//'; } > "$out"
    continue
  fi

  # shellcheck disable=SC2086
  res=$(cd "$dir" && "$lowentc" --check $flags "$name" </dev/null 2>&1)
  if ! printf '%s\n' "$res" | grep -q 'check: ok'; then
    echo "  [FAIL] $rel — --check 가 통과하지 않는다"; printf '%s\n' "$res" | grep -E '[EW]-' | head -5; fail=$((fail + 1)); continue
  fi
  if grep -q '^rem test-fail *$' "$src"; then
    tres=$(cd "$dir" && "$lowentc" --test "$name" </dev/null 2>&1); trc=$?
    if [ "$trc" -eq 0 ]; then
      echo "  [FAIL] $rel — --test 가 실패해야 하는데 통과했다"; fail=$((fail + 1)); continue
    fi
    { echo "\$ lowentc --test $name"; printf '%s\n' "$tres"; } > "$out.test"
  fi
  if grep -q '^rem test *$' "$src"; then
    tres=$(cd "$dir" && "$lowentc" --test "$name" </dev/null 2>&1); trc=$?
    if [ "$trc" -ne 0 ]; then
      echo "  [FAIL] $rel — --test 가 실패했다"; printf '%s\n' "$tres" | tail -5; fail=$((fail + 1)); continue
    fi
    { echo "\$ lowentc --test $name"; printf '%s\n' "$tres"; } > "$out.test"
  fi
  if [ -z "$runs" ] && [ -z "$traps" ]; then
    if [ -f "$out.test" ]; then mv "$out.test" "$out"
    elif grep -q '^rem db *$' "$src"; then
      { echo "\$ lowentc --emit-db $name"; (cd "$dir" && "$lowentc" --emit-db "$name" </dev/null 2>/dev/null) | grep -E '^(fn|proc) '; } > "$out"
    else { echo "\$ lowentc --check $name"; echo "== check: ok =="; } > "$out"; fi
    continue
  fi

  exe="$bindir/$(echo "$rel" | tr '/.' '__')"
  if ! (cd "$dir" && "$lowentc" $flags --emit-c "$name" > "$exe.c" 2>/dev/null) || \
     ! "$cc" -O2 -w -o "$exe" "$exe.c" -lm -lpthread 2>"$exe.cc.log"; then
    echo "  [FAIL] $rel — 네이티브로 짓지 못했다"; head -3 "$exe.cc.log"; fail=$((fail + 1)); continue
  fi
  : > "$out"
  bad=0
  echo "$runs" | while IFS= read -r line; do
    [ -n "$line" ] || continue
    op=$(echo "$line" | cut -d' ' -f1); args=$(echo "$line" | cut -s -d' ' -f2-)
    # shellcheck disable=SC2086
    vm=$(cd "$dir" && eval "\"$lowentc\" $flags --run $op \"$name\" $args" </dev/null 2>&1)
    # shellcheck disable=SC2086
    nat=$(cd "$dir" && eval "\"$exe\" $line" </dev/null 2>&1); rc=$?
    want=$vm
    if [ "$op" = main ]; then
      # 네이티브의 `main` 은 프로그램이다 — 값은 출력 줄이 아니라 종료 코드로 나온다
      last=$(printf '%s\n' "$vm" | tail -n 1)
      code=$(printf '%s' "$last" | sed -n 's/^main() = \([0-9][0-9]*\)$/\1/p')
      if [ -n "$code" ]; then
        want=$(printf '%s\n' "$vm" | sed '$d')
        [ "$((code % 256))" = "$rc" ] || want="(종료 코드가 다르다: VM $code · 네이티브 $rc)"
      fi
    fi
    if [ "$want" != "$nat" ]; then
      echo "  [FAIL] $rel — VM 과 네이티브가 다르다: $line" >&2
      echo "    VM:  $(printf '%s' "$vm" | tail -n 2 | tr '\n' '|')" >&2
      echo "    NAT: $(printf '%s' "$nat" | tail -n 2 | tr '\n' '|')" >&2
      echo BAD > "$out.bad"
    fi
    { echo "\$ lowentc ${flags:+$flags }--run $op $name${args:+ $args}"; printf '%s\n' "$vm"; } >> "$out"
  done
  echo "$traps" | while IFS= read -r line; do
    [ -n "$line" ] || continue
    op=$(echo "$line" | cut -d' ' -f1); args=$(echo "$line" | cut -s -d' ' -f2-)
    vm=$(cd "$dir" && eval "\"$lowentc\" --run $op \"$name\" $args" </dev/null 2>&1); vrc=$?
    nat=$(cd "$dir" && eval "\"$exe\" $line" </dev/null 2>&1); nrc=$?
    if [ "$vrc" -eq 0 ] || [ "$nrc" -eq 0 ]; then
      echo "  [FAIL] $rel — 멈춰야 하는데 멈추지 않았다: $line (VM $vrc · 네이티브 $nrc)" >&2
      echo BAD > "$out.bad"
    fi
    { echo "\$ lowentc --run $op $name${args:+ $args}"; printf '%s\n' "$vm" | sed 's/^ *//'; } >> "$out"
  done
  if [ -f "$out.test" ]; then cat "$out.test" >> "$out"; rm -f "$out.test"; fi
  if grep -q '^rem fmt *$' "$src"; then
    { echo "\$ lowentc --fmt $name"; (cd "$dir" && "$lowentc" --fmt "$name" </dev/null 2>/dev/null); } >> "$out"
  fi
  if grep -q '^rem ir *$' "$src"; then
    { echo "\$ lowentc --ir $name"; (cd "$dir" && "$lowentc" --ir "$name" </dev/null 2>&1) | grep -A1 '^-- runtime checks'; } >> "$out"
  fi
  if [ -f "$out.bad" ]; then rm -f "$out.bad"; fail=$((fail + 1)); fi
done

# C 호스트: 첫 줄이 `// host-for: <x>.low` 인 C 파일은 그 모듈을 --no-main 으로 내보내 함께 짓고 돌린다.
# 계약이 경계를 지키는지 보이려고 멈추는 호출도 싣는다 — 종료 코드는 캡처 끝줄에 적는다.
for host in $(find "$root/examples" -name '*.c' | sort); do
  lowmod=$(sed -n '1s|^// host-for: *||p' "$host")
  [ -n "$lowmod" ] || continue
  total=$((total + 1))
  rel=${host#"$root/examples/"}; dir=$(dirname "$host")
  work="$bindir/$(echo "$rel" | tr '/.' '__')"; mkdir -p "$work"
  out="$outdir/$rel.out"; mkdir -p "$(dirname "$out")"
  if ! (cd "$dir" && "$lowentc" --emit-c --no-main "$lowmod" > "$work/mod.c" 2>/dev/null && "$lowentc" --emit-h "$lowmod" > "$work/lowent.h" 2>/dev/null) || \
     ! "$cc" -O2 -w -I"$work" -o "$work/host" "$host" "$work/mod.c" -lm -lpthread 2>"$work/cc.log"; then
    echo "  [FAIL] $rel — C 호스트를 짓지 못했다"; head -3 "$work/cc.log"; fail=$((fail + 1)); continue
  fi
  res=$("$work/host" </dev/null 2>&1); rc=$?
  { echo "\$ cc host.c <$lowmod 을 --no-main 으로 낸 C> && ./host"; printf '%s\n' "$res"; echo "(종료 코드 $rc)"; } > "$out"
done

echo "verify-examples: 예제 $total · 실패 $fail"
[ "$fail" -eq 0 ]
