# Vendored: proven_c_lib

- Upstream: `../../../../proven_c_lib`
- Version: `proven_c_lib-v0.113.0` (태그 `v0.113.0`)
- Vendored: 2026-11-02 v0.108.0 (was v0.107.3 / `9d26e22f` / 같은 날 · 그전 v0.107.2 / `def42831` / 같은 날 · 그전 전체 재벤더 was v0.6.0 / `e62cee2` / 2026-10-02 · 그전 v0.1.1 / `22f964e` / 2026-09-22 · 그전 v0.1.0 / `e30a517` / 2026-09-12 · v26.07.23d / `c0e4d09` / 2026-07-24 · v26.07.13m+두파일 혼합 / v26.06.24b / 1d64286)

> **2026-11-12 재벤더 (v0.108.0 → v0.113.0 · W-20261112-0207, 소유자 «메모리 할당 · 입출력 포매팅을 proven 으로» · 판은 «v0.113.0 으로 올리고 시작»).**
> `git archive v0.113.0 include src platform LICENSE THIRD_PARTY_NOTICES.md` 로 셋을 통째로 바꿨다(옛 셋을 지우고 풀었다 — 상류에서 지운 파일도 빠진다).
> 지워진 파일 99 · 새 파일 1(`src/proven/u16str_view.c`). 지워진 것은 상류가 `src/` 밖(`codec/` · `crypto/` · `net/`)으로 옮긴 압축 · 암호 · 그물 모듈과
> 그 platform 짝(`proven_sys_aes` · `chacha` · `cpu` · `net` · `trust`)이다 — 처리기는 그중 하나도 짓지 않았다. 짓는 목록은 그대로다.
> ★ 확인: 처리기의 겉 출력 전부(시험 · 라이브러리 · 매뉴얼 예제 1,076 파일 × 여덟 모드, `scripts/proven-io-oracle.py`)가 `--version` 한 줄 말고는 바이트까지 같다.
> ★ 처리기의 Makefile 은 vendor 머리글을 의존으로 보지 않는다 — 재벤더 뒤에는 `make clean && make` 를 해야 새 판이 엮인다(`--version` 으로 확인).

> **2026-11-02 재벤더 (v0.107.3 → v0.108.0 · W-20261102-2330, 소유자 «16진 부동소수 읽기 가능하지 않을까요»).** `git archive v0.108.0` 으로 같은 다섯을 바꿨다.
> 내용이 바뀐 파일은 셋이다: `include/proven/float_parse.h` · `src/proven/float_parse.c` · `include/proven/version.h`.
> ★ `proven_parse_double_ascii` · `proven_strtod` 가 C 의 십육진 부동 꼴을 읽고(가장 가까운 값 · 동률은 짝수), `proven_parse_double_ascii` 가 f64 를
>   넘는 수를 `PROVEN_ERR_OVERFLOW`(± 무한과 그 길이)로 따로 알린다 — 둘 다 이 저장소가 상류에 낸 요청이다. 처리기는 이제 부동 리터럴과
>   `--run` 의 부동 인자를 모두 `proven_parse_double_ascii` 로 읽는다(`low_token.h` · `main.c`) — C 의 strtod 는 처리기의 읽기에 없다
>   (네이티브 프로그램의 인자 읽기는 proven 을 엮지 않으므로 그대로 C 의 strtod). 짓는 목록은 그대로다(아래 «What is compiled»).

> **2026-11-02 재벤더 (v0.107.2 → v0.107.3 · W-20261102-2224, 소유자 «brotli 사전고지 바랍니다»).** `git archive v0.107.3` 으로 같은 다섯을 바꿨다.
> 내용이 바뀐 파일은 넷이다: `THIRD_PARTY_NOTICES.md`(+44 줄) · `include/proven/version.h` · `src/proven/qpack.c` · `src/proven/zstd_encode.c`.
> ★ **Brotli 사전의 고지가 이제 함께 실린다.** `src/proven/brotli_dict.h` 는 RFC 7932 의 본문에서 만든 표(부록 A 사전 · 부록 B 변환 ·
>   7.1 절 문맥 표)이고, 상류가 v0.107.3 에서 `THIRD_PARTY_NOTICES.md` 에 «Brotli static dictionary and transforms (RFC 7932)» 절을 더했다 —
>   그 표를 밝히고, 그것이 Code Component 에 든다면 따라야 할 IETF Trust 의 라이선스 글(TLP 4.e)을 옮겼다. 처리기는 그 파일을 짓지 않지만
>   공개 저장소에 그대로 실리므로 고지도 함께 가야 한다. lowent 의 `LICENSE`(MIT)는 그대로다.
> ★ 나머지 둘(`qpack` · `zstd_encode`)은 처리기가 짓지 않는다. 짓는 열넷(아래 «What is compiled»)은 바이트까지 같다 — `version.h` 의 판 문자열만
>   달라져 `lowentc --version` 이 `proven_c_lib-v0.107.3` 을 찍는다. 상류의 16진 부동 읽기(`a4144971`)는 아직 태그가 없어 들여오지 않았다.

> **2026-11-02 재벤더 (v0.6.0 → v0.107.2, 상류 431 커밋 · W-20261102-1140).** 태그를 `git archive v0.107.2` 로 꺼내
> `include/`·`src/`·`platform/`·`LICENSE`·`THIRD_PARTY_NOTICES.md` 를 통째로 바꿨다. 바뀐 파일 25, 새 파일 101, 지워진 파일 없음.
> 새 파일은 거의 다 상류의 네트워크·압축·암호 모듈이다(TLS 1.3 · QUIC · HTTP/1·2·3 · WebSocket · deflate · brotli · zstd · 해시 ·
> 인증서). 처리기는 그중 하나도 짓지 않는다 — 들여온 C 가 약 27 천 줄에서 94 천 줄로 늘었고(`find -name '*.[ch]' | xargs cat | wc -l`),
> 그 늘어난 것은 헤더 완결성으로만 있다. `LICENSE`·`THIRD_PARTY_NOTICES.md` 는 바뀌지 않았다.
> 처리기가 부르는 proven 함수(링크되는 것 17 개 — `nm -u build/src/*.o`)는 하나도 바뀌지 않았다. 상류는 더하기만 했다.
> ★★ **고친 것 하나 — VM 의 `time_now` 가 벽시계였다.** v0.6.0 의 `platform/proven_sys_time.h` 는
>   `proven_sys_time_now_ns` 를 «high-resolution monotonic time» 이라 적었지만 실제로는 CLOCK_REALTIME(윈도는 FILETIME)이었다.
>   VM 은 그 설명을 믿고 `time_now`·`time_sleep` 에 그것을 썼고, 네이티브는 CLOCK_MONOTONIC 을 썼다 — 정본(부록 D «단조 시계»)·
>   안내서(«never goes backwards») 와 맞는 쪽은 네이티브였다. 상류 v0.107.2 가 그 머리글을 «Not monotonic» 으로 바로잡고 진짜
>   단조 시계 `proven_sys_time_monotonic_ns` 를 더했다. VM 이 그것을 쓴다. 증인 `tests/vm_clock.low` `t_origin`(옛 VM 90 · 새 VM 1 ·
>   네이티브 1).
> ★ **짓는 목록에서 다섯을 뺐다**: `memory` · `pool` · `scan` · `float_parse` · `float_decimal`. 짓고 링크했지만 처리기 오브젝트도
>   다른 proven 오브젝트도 그 심볼을 찾지 않았다(`nm`). 아래 2026-10-02 블록의 «부동소수 해석은 빨라졌고 답은 같다고 … 코퍼스로
>   확인했다» 는 **아무것도 확인하지 않은 확인**이었다 — 처리기는 부동소수를 `strtod` 로 읽고 `%g` 로 찍는다. 그래서 상류의
>   부동소수 고침(낮은 림이 0 인 128 비트 값 · u256 이동의 UB)도 처리기에 닿지 않는다.
>   ☞ 같은 날 뒤이어(W-20261102-1435) 소유자 요청으로 부동 읽기를 `proven_strtod` 로 바꿨다 — `float_parse`·`float_decimal` 이 짓는
>   목록에 돌아왔고, 이제 그 고침이 처리기에 닿는다(아래 «What is compiled»).
> ★ 확인: 옛 처리기(v0.6.0)와 새 처리기의 방출 C·`--check` 출력이 515 파일(`tests` · `lib` · `docs/example` 의 `.low`)에서
>   바이트까지 같다. `make test` · `make alloccheck` 초록 · gcc 벤더 경고 0. `--version` 출력이 `proven_c_lib-v0.107.2` 로 바뀐다.

> **2026-10-02 재벤더 (v0.1.1 → v0.6.0, 상류 83 커밋).** 태그를 `git archive v0.6.0` 으로 꺼내 `include/`·`src/`·`platform/`·
> `LICENSE`·`THIRD_PARTY_NOTICES.md` 를 통째로 바꿨다. 바뀐 파일 83, 새 파일 다섯(`include/proven/{alloc_check,utf}.h` ·
> `src/proven/{alloc_check,utf}.c` · `src/proven/proven_internal_console.h`), 지워진 파일 없음. 상류는 0.2~0.6 에서 공개 API 를
> 지우지 않았다(MINOR 판들).
> ★ `PROVEN_SRC` 가 짓는 열넷이 모두 바뀌었다 — 새 파일은 하나도 짓지 않아도 링크됐다. 같은 날 뒤이어 새 기능을 쓰려고
>   `utf`·`u16str`·`alloc_check` 셋을 짓는 목록에 더했다(아래 «What is compiled»).
>   lowentc 가 쓰는 proven 함수는 36 개이고, 상류의 동작 변경(맵 정수 열쇠 해시 · 플랫폼 글자 엄격 · 배타 생성의
>   `PROVEN_ERR_EXISTS` · `find_last` · 리더 칸 추가)은 lowentc 가 부르지 않는 자리다. 부동소수 해석은 빨라졌고 답은 같다고
>   상류가 적었다 — 여기서도 코퍼스로 확인했다.
> ★ 확인: 옛 처리기(HEAD, v0.1.1)와 새 처리기(v0.6.0)의 방출 C·검사 결과가 929 파일에서 바이트까지 같다(실행마다 흔들리는
>   5 파일 제외) · `make test` · arch-dev clang 시험 초록(벤더 경고 0) · gcc 벤더 경고 0. `--version` 출력이
>   `proven_c_lib-v0.6.0` 으로 바뀐다(그 문자열을 기대하는 자리는 없다).

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

오늘(2026-11-12) `PROVEN_SRC` 가 짓는 것: `arena · buffer · u8str · array · panic · heap ·
float_parse · float_decimal · fmt · float_format · utf · u16str · alloc_check`(13) + `platform/proven_sys_{mem,time,random}.c`(3).
(`float_parse`·`float_decimal` 은 부동 리터럴과 부동 인자 읽기 · `utf` 는 소스 검증과 `u"…"` 리터럴의 해독 · `u16str` 은 `utf` 가 부르는 덧붙이기 하나 · `buffer` 는 `u8str`·`u16str` 이 부른다 ·
`alloc_check` 은 `make alloccheck` 용이고 `panic` 은 그것이 부른다. 2026-11-02 에 아무도 부르지 않던 `memory · pool · scan ·
float_parse · float_decimal` 다섯을 뺐다 — 그리고 같은 날 소유자 요청으로 `float_parse · float_decimal` 둘이 돌아왔다: 부동 리터럴과
`--run` 의 부동 인자를 `proven_strtod` 가 읽는다. 이제 상류의 부동소수 고침(낮은 림이 0 인 128 비트 값 · u256 이동의 UB)이 처리기에 닿는다.)
2026-11-12(W-20261112-0207 S2): `fmt · float_format` — 처리기가 버퍼에 글을 짓는 자리(전엔 libc `snprintf`)를 proven 의 포매터가 한다
(`src/low_fmt.h` 의 `low_bfmt`). ★ 다시 들여올 때 볼 것: `low_fmt.h` 는 `fmt.h` 의 `proven_u8str_fmt_internal` 을 부른다 — fmt.h 가
«안정된 인터페이스가 아니다(MINOR 판에서 서명이 바뀔 수 있다)» 고 적은 함수다. C 와 다르게 받아들인 것: 잘라야 할 때 UTF-8 글자를
가르지 않는다 · `%s` 의 NULL 은 빈 글(glibc `(null)`). libc 에 남긴 것: 부동소수 `%g`/`%e` 자리 — proven 의 `{:g}` 는 가장 짧은 왕복 꼴이라
printf `%g` 와 글자가 다르다(상류 요청 `proven_c_lib/docs/requirements/2026-11-12-lowent-printf-g-mode.md`).
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
completeness, but only the core `.c` above are built. Everything else (fs, sysio,
mmap, time, coro, job, float_*, scan, and since v0.107.2 net · tls · http · compression) is
present but not compiled.

## Updating

Re-copy `include/ src/ platform/ LICENSE THIRD_PARTY_NOTICES.md` from upstream and
bump the version line above. If upstream layering changes, re-verify the compiled
set. Report defects found while using proven back to upstream (per prov_text_editor
vendor convention: a REPORT.md).
