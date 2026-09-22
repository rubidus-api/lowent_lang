# Vendored: proven_c_lib

- Upstream: `../../../../proven_c_lib`
- Version: `proven_c_lib-v0.1.1` (git `22f964e` — 태그 `v0.1.1`)
- Vendored: 2026-09-22 전체 재벤더 (was v0.1.0 / `e30a517` / 2026-09-12 · 그전 v26.07.23d / `c0e4d09` / 2026-07-24 · v26.07.13m+두파일 혼합 / v26.06.24b / 1d64286)

> **2026-09-22 재벤더 (v0.1.0 → v0.1.1).** 바뀐 파일 여덟: `include/proven/version.h` 와
> `platform/proven_sys_{env,io,math,mem,random,thread,time}.c`. PAL 소스 앞머리에 `_DEFAULT_SOURCE`·
> `_POSIX_C_SOURCE` 정의가 들어갔을 뿐이다(`#ifndef` 가드, `_WIN32` 제외) — `-D` 없이 `-std=c23` 으로
> 짓는 손 빌드가 GCC 14/glibc 에서 실패하던 것을 상류가 고쳤다. ★ `PROVEN_SRC` 의
> `proven_sys_{mem,time,random}.c` 가 이 여덟에 든다. 그리고 `--version` 출력이 `proven_c_lib-v0.1.1` 로 바뀐다.
- License: MIT (see `LICENSE`). `THIRD_PARTY_NOTICES.md` covers upstream's vendored `nob.h`.

> **2026-09-12 재벤더 (X-0025 · WO-0202).** 상류가 44 커밋 앞서 있었다. 판 번호 규약도 바뀌었다:
> 날짜판(`v26.MM.DDx`)에서 **의미판**(`v0.1.0`)으로 — `PROVEN_VERSION_NUM` 은 `PROVEN_VERSION_ENCODE(0,1,0)`
> 이고, 날짜판보다 **작은 수**다(`260723` → `1000`). 판 번호를 크기로 견주는 코드가 있으면 그 자리가 뒤집힌다
> — lowentc 에는 **견주는** 자리가 없다(`grep PROVEN_VERSION` = `src/main.c` 두 줄뿐).
> ★ 다만 그 두 줄은 `PROVEN_VERSION_STRING` 을 **`--version` 에 찍는다** — 즉 이 재벤더는 사용자가 보는
>   출력을 바꾼다(`proven_c_lib-v26.07.23d` → `proven_c_lib-v0.1.0`). 그 문자열을 기대하는 자리도 함께 고쳤다.
>
> 바뀐 파일 열둘: `include/proven/{alias_xcv,encode,fs,version}.h` · `src/proven/{encode,fs,job}.c` ·
> `platform/proven_sys_{fs.c,fs.h,random.c}` · 새 파일 둘 `src/proven/proven_internal_jobseq.h` ·
> `platform/proven_sys_random_chunk.h`. 지워진 파일은 없다.
>
> `platform/` 이 바뀐 요지(상류 세션 확인, 2026-09-12): 윈도 파일 교체가 POSIX 식 rename(1809+)을 먼저 쓰고
> 안 되면 `MoveFileExW` 로 물러선다 · 거절 이유를 PERMISSION/BUSY 로 가른다 · 읽기 전용(보호된) 대상은 모든
> 전체쓰기 경로에서 거절한다 · 엔트로피는 4 GiB 이상 요청을 쪼개 채운다. 상류는 이 변경을 Windows 11 에서
> x86-64·i686·대체경로로 41 검사 무실패(NTFS·FAT32·exFAT·SMB) 확인했다.
>
> ★ **짓는 집합에 든 것이 하나 있다**: `platform/proven_sys_random.c`(+ 새 헤더 `proven_sys_random_chunk.h`).
>   백로그 X-0025 의 메모는 *"짓는 열둘은 한 줄도 안 바뀌었다"* 고 적었지만 `PROVEN_SRC` 에는
>   `proven_sys_{mem,time,random}.c` 도 들어 있다 — **컴파일되는 것과 컴파일되는 목록이 두 곳에서 세어졌다.**
>   나머지 아홉은 `fs`·`encode`·`job` 계열이라 lowentc 가 안 짓는다(헤더 완결성으로만 있다).

> **단일 버전이다.** 2026-07-24 에 `include/`·`src/`·`platform/` 전체를 상류 `c0e4d09`
> (v26.07.23d)에서 재벤더해, 직전의 혼합(v26.07.13m 기반 + `panic.c`·`map.c` 두 파일만
> 23d)을 없앴다. 상류 `c0e4d09` 는 크로스플랫폼 빌드 하드닝 커밋이다 — atomic 표기를 C11
> `atomic_*_explicit` 로(clang 이 함수 포인터 `_Atomic` 에 GCC `__atomic_*` 빌트인을 거부),
> Windows CSPRNG `#pragma comment(lib)` 를 `_MSC_VER` 로 가드(mingw 는 `-lbcrypt`).
> 근거·이력: `docs/known-defects/vendored-proven-atomic-clang.md`

## Why

lowent-mini's runtime memory model (SPEC-MINI §8.1) is **arena + bulk-free**, which
is exactly proven's region allocator. We reuse proven for: arena/allocator (memory),
`u8str_view` (zero-copy token/lexeme slices), `array` (token/child lists), `map`
(symbol tables), plus result-struct error handling and C23 `[[nodiscard]]` discipline.

## What is compiled (see ../../Makefile `PROVEN_SRC`)

★★★ **목록의 정본은 `../../Makefile` 의 `PROVEN_SRC` 다 — 여기 베껴 적지 않는다.**
2026-09-12 에 이 문장이 틀렸다는 것이 드러났다: `algorithm` 을 짓는다고 적었지만 `PROVEN_SRC` 에 없고,
실제로 짓는 `platform/proven_sys_time.c`·`proven_sys_random.c` 는 빠져 있었다. 그 틀린 문장을
백로그(X-0025)·요청(REQ-0017)·상류의 다운스트림 알림이 **그대로 베껴 썼고**, 그래서 이번 재벤더에서
*"짓는 것은 한 줄도 안 바뀌었다"* 는 잘못된 안심이 세 곳에 퍼졌다(실제로는 `proven_sys_random.c` 가 바뀌었다).
☞ *같은 목록을 두 곳에 두면 한 곳만 낡고, 낡은 쪽이 인용된다*(교훈 7).

오늘(2026-09-12) `PROVEN_SRC` 가 짓는 것: `arena · memory · buffer · u8str · array · panic · heap ·
pool · scan · float_parse · float_decimal`(11) + `platform/proven_sys_{mem,time,random}.c`(3).
Warning-clean under `-std=c23 -Wall -Wextra`(벤더 경고 0 — 남아 있던 둘은 우리 쪽 `PROVEN_ARRAY_GET`
오용이었고 `GET_MUT` 으로 고쳤다).

★ v26.07.13m note: `map` was dropped from the compiled set — lowentc uses its OWN symbol
tables (its sources include only `arena`/`array`/`heap`), so `map.c` was dead code. The new
`map` uses SipHash-2-4 (HashDoS defence, a keyed RANDOM seed) which would (a) pull in
`hash`/`random`/`proven_sys_thread` and (b) make map iteration non-deterministic — neither is
wanted for an unused module. If a future lowentc ever needs proven's map for TRUSTED internal
keys, use `proven_map_create_trusted` (unkeyed FNV-1a: fast AND deterministic), not the
SipHash default.

The full `include/`, `src/`, and `platform/` trees are vendored for header
completeness, but only the core `.c` above are built. OS-heavy modules (fs, sysio,
mmap, time, coro, job, u16str, float_*) are present but not compiled unless needed.

## Updating

Re-copy `include/ src/ platform/ LICENSE THIRD_PARTY_NOTICES.md` from upstream and
bump the version line above. If upstream layering changes, re-verify the compiled
set. Report defects found while using proven back to upstream (per prov_text_editor
vendor convention: a REPORT.md).
