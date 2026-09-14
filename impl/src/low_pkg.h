// low_pkg — pkg.low 매니페스트 (RFC-0032/0033 · 2A)
//
// ★ 매니페스트는 **새 문법이 아니다.** 이미 있는 평면 선언 폼들의 **닫힌 서브셋**이다:
//     package <key> <value> .                    (정체 — ck_package 가 이미 검증하는 그 폼)
//     use <name> from "<source>" [hash "<hex>"] .  (의존 — 프로그램의 use 폼 그대로 + 해시 핀)
//     build <key> <value> .                      (빌드 — build profile/tier 가 이미 있는 그 폼)
//   E-PKG-FORM 의 교훈 그대로: 블록(`package … end`)은 새 키워드가 필요했고, 닫힌 어휘는
//   43 인 이유가 있다. 선언 폼 하나면 충분하다.
//
// ★ 선언적이다(RFC-0032 D1): 이 파일은 **실행되지 않는다**. 파서는 토큰만 읽고,
//   모르는 폼은 **거절한다**(읽히지 않는 키는 검사되지 않는 키다 — PRINCIPLES.md §0).
#ifndef LOW_PKG_H
#define LOW_PKG_H

#include "low_lex.h"

typedef struct {
    char        name[64];      // use <name>
    char        source[256];   // from "<source>" — 경로(매니페스트 dir 기준) 또는 URL
    char        hash[65];      // hash "<hex64>" — 내용 핀. 없으면 "" (lock 이 핀한다)
    proven_u32  line;
} low_pkg_dep_t;

typedef struct {
    bool           found;         // pkg.low 를 찾았는가
    char           dir[512];      // pkg.low 가 있는 디렉터리 (의존 경로의 기준)
    char           path[600];     // pkg.low 전체 경로
    char           name[64];      // package name …
    char           version[32];   // package version …
    char           entry[256];    // build entry "<file>" (없으면 "")
    char           profile[64];   // build profile <word>
    char           target[64];    // build target <word>
    low_pkg_dep_t  deps[32];
    int            ndeps;
} low_pkg_t;

// <start_dir> 에서 위로 걸어(최대 8단) pkg.low 를 찾는다. 찾으면 out->dir/path 채우고 true.
bool low_pkg_find(const char *start_dir, low_pkg_t *out);

// out->path 의 pkg.low 를 읽어 파싱한다. 문법 위반이면 false + err 에 한 줄 진단.
bool low_pkg_load(proven_allocator_t heap, low_pkg_t *out, char err[512]);

// 이름으로 선언 의존을 찾는다. 없으면 NULL.
const low_pkg_dep_t *low_pkg_dep_find(const low_pkg_t *p, const char *name);

#endif
