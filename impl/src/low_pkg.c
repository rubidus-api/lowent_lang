// low_pkg — pkg.low 매니페스트 파서 (RFC-0032/0033 · 2A)
//
// ★★★ **의존 선언이 소스 밖으로 나왔다** — 지금까지 의존은 각 .low 의 `use … from` 이
//   말했고, 그것으로 충분했다(파일 하나 = 의존 하나). 그런데 from-생략 해소(RFC-0011 §6.3)의
//   2계층 — *"매니페스트가 선언한 의존"* — 이 서려면 **프로젝트가 자기 의존을 한 곳에서
//   말하는 파일**이 필요하다. 그것이 pkg.low 다.
//
// ★ 문법은 **전부 재사용**이다: `package k v .`(ck_package 의 그 폼) · `use n from "s" .`
//   (deps_of 의 그 폼 + `hash "<hex>"` 핀) · `build k v .`(build profile 의 그 폼).
//   낱말 0개 추가. 렉서도 그대로(rem 주석·문자열·IDENT 전부 공짜).
//
// ★★ 그리고 **모르는 폼은 거절한다.** 매니페스트는 도구가 소스 전체를 읽지 않고 믿으려고
//   존재하는 파일이다 — 아무도 안 읽는 줄이 섞일 수 있으면 그 신뢰가 선다.
//   (E-PKG-KEY 의 논증 그대로: 읽히지 않는 키는 검사되지 않는 키다.)
#define _GNU_SOURCE   // realpath(3) — -std=c23 은 POSIX 선언을 숨긴다
#include "low_pkg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool tveq(const low_token_t *t, const char *s) {
    proven_size_t n = 0; while (s[n]) n++;
    return t->kind == LOW_TOK_IDENT && t->lex.size == n && memcmp(t->lex.ptr, s, n) == 0;
}

static void tcopy(char *dst, proven_size_t cap, const low_token_t *t) {
    proven_size_t n = t->lex.size < cap - 1 ? t->lex.size : cap - 1;
    memcpy(dst, t->lex.ptr, n); dst[n] = 0;
}

bool low_pkg_find(const char *start_dir, low_pkg_t *out) {
    memset(out, 0, sizeof *out);
    // ★ 먼저 **절대화**한다 — 상대 경로("src")에서 성분을 지우며 올라가면 첫 단에서 빈
    //   문자열이 되어 걷기가 멈췄다(실측: src/ 아래에서 프로젝트 루트의 pkg.low 를 못 봤다).
    //   realpath 위에서는 "성분 하나 지우기 = 한 단 위" 가 늘 참이다.
    char dir[512];
    if (!realpath(start_dir[0] ? start_dir : ".", dir))
        snprintf(dir, sizeof dir, "%s", start_dir[0] ? start_dir : ".");
    for (int depth = 0; depth < 8; depth++) {
        char cand[600];
        snprintf(cand, sizeof cand, "%s/pkg.low", dir);
        FILE *f = fopen(cand, "rb");
        if (f) {
            fclose(f);
            snprintf(out->dir, sizeof out->dir, "%s", dir);
            snprintf(out->path, sizeof out->path, "%s", cand);
            out->found = true;
            return true;
        }
        size_t n = strlen(dir);
        while (n > 0 && dir[n - 1] == '/') n--;
        while (n > 0 && dir[n - 1] != '/') n--;
        while (n > 1 && dir[n - 1] == '/') n--;
        if (n == 0) break;   // 루트를 지났다
        dir[n] = 0;
        if (dir[0] == 0) break;
    }
    return false;
}

const low_pkg_dep_t *low_pkg_dep_find(const low_pkg_t *p, const char *name) {
    for (int i = 0; i < p->ndeps; i++)
        if (strcmp(p->deps[i].name, name) == 0) return &p->deps[i];
    return NULL;
}

bool low_pkg_load(proven_allocator_t heap, low_pkg_t *out, char err[512]) {
    err[0] = 0;
    FILE *f = fopen(out->path, "rb");
    if (!f) { snprintf(err, 512, "cannot read manifest: %s", out->path); return false; }
    static char buf[65536];
    size_t len = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (len == sizeof buf) { snprintf(err, 512, "manifest too large (>64KiB): %s", out->path); return false; }

    proven_u8str_view_t sv = { .ptr = (const proven_u8 *)buf, .size = len };
    low_lex_result_t lx = low_lex(heap, sv);
    const low_token_t *t = (const low_token_t *)lx.tokens.data;
    proven_size_t n = lx.tokens.len;

    // 폼 단위로 읽는다: <머리> … DOT. 닫힌 세 머리(package/use/build)만 산다.
    proven_size_t i = 0;
    while (i < n && t[i].kind != LOW_TOK_EOF) {
        // 폼의 끝(다음 고립 DOT) 을 먼저 찾는다
        proven_size_t e = i;
        while (e < n && t[e].kind != LOW_TOK_DOT && t[e].kind != LOW_TOK_EOF) e++;
        proven_size_t nf = e - i;   // DOT 을 뺀 폼 토큰 수
        if (nf == 0) { i = e + 1; continue; }
        if (e >= n || t[e].kind == LOW_TOK_EOF) {
            snprintf(err, 512, "pkg.low:%u E-PKG-FORM: a declaration must end with `.` — the manifest is the same flat form language as the source", t[i].line);
            return false;
        }

        if (tveq(&t[i], "package")) {
            // package <key> <value…> . — 정체 키는 ck_package 의 닫힌 집합. 여기선 name/version 만 담고
            // 나머지 알려진 키(description/date/authors/license)는 통과시킨다.
            if (nf < 3) { snprintf(err, 512, "pkg.low:%u E-PKG-FORM: `package <key> <value> .`", t[i].line); return false; }
            static const char *PKEYS[] = { "name", "version", "description", "date", "authors", "license" };
            bool known = false;
            for (int q = 0; q < 6; q++) if (tveq(&t[i + 1], PKEYS[q])) known = true;
            if (!known) { snprintf(err, 512, "pkg.low:%u E-PKG-KEY: unknown `package` key — the manifest keys are a CLOSED set (name · version · description · date · authors · license)", t[i + 1].line); return false; }
            if (tveq(&t[i + 1], "name"))    tcopy(out->name, sizeof out->name, &t[i + 2]);
            if (tveq(&t[i + 1], "version")) tcopy(out->version, sizeof out->version, &t[i + 2]);
        } else if (tveq(&t[i], "use")) {
            // use <name> from "<source>" [hash "<hex64>"] .
            if (nf < 4 || t[i + 1].kind != LOW_TOK_IDENT || !tveq(&t[i + 2], "from") ||
                t[i + 3].kind != LOW_TOK_STRING) {
                snprintf(err, 512, "pkg.low:%u E-PKG-DEP: a dependency is `use <name> from \"<source>\" [hash \"<hex>\"] .` — the same `use` form the source itself writes", t[i].line);
                return false;
            }
            if (out->ndeps >= 32) { snprintf(err, 512, "pkg.low:%u E-PKG-DEP: too many dependencies (max 32)", t[i].line); return false; }
            low_pkg_dep_t *d = &out->deps[out->ndeps];
            memset(d, 0, sizeof *d);
            tcopy(d->name, sizeof d->name, &t[i + 1]);
            tcopy(d->source, sizeof d->source, &t[i + 3]);
            d->line = t[i].line;
            if (nf >= 6 && tveq(&t[i + 4], "hash")) {
                if (t[i + 5].kind != LOW_TOK_STRING || t[i + 5].lex.size != 64) {
                    snprintf(err, 512, "pkg.low:%u E-PKG-HASH: `hash` pins the CONTENT — it must be the 64-hex BLAKE3 of the dependency's bytes", t[i + 4].line);
                    return false;
                }
                tcopy(d->hash, sizeof d->hash, &t[i + 5]);
            } else if (nf != 4) {
                snprintf(err, 512, "pkg.low:%u E-PKG-DEP: unexpected words after the dependency source (only `hash \"<hex>\"` may follow)", t[i].line);
                return false;
            }
            // 같은 이름 두 번 = 어느 쪽을 믿어야 하는지 아무도 모른다
            for (int q = 0; q < out->ndeps; q++)
                if (strcmp(out->deps[q].name, d->name) == 0) {
                    snprintf(err, 512, "pkg.low:%u E-PKG-DUP: dependency `%s` is declared twice — two declarations for one name means every reader must pick, and they will not all pick the same one", t[i].line, d->name);
                    return false;
                }
            out->ndeps++;
        } else if (tveq(&t[i], "build")) {
            // build <key> <value> . — 이 매니페스트가 아는 빌드 키는 entry/profile/target 뿐.
            if (nf < 3) { snprintf(err, 512, "pkg.low:%u E-PKG-FORM: `build <key> <value> .`", t[i].line); return false; }
            if      (tveq(&t[i + 1], "entry"))   tcopy(out->entry,   sizeof out->entry,   &t[i + 2]);
            else if (tveq(&t[i + 1], "profile")) tcopy(out->profile, sizeof out->profile, &t[i + 2]);
            else if (tveq(&t[i + 1], "target"))  tcopy(out->target,  sizeof out->target,  &t[i + 2]);
            else { snprintf(err, 512, "pkg.low:%u E-PKG-KEY: unknown `build` key in the manifest — it knows entry · profile · target (a key nobody reads is a key nobody checks)", t[i + 1].line); return false; }
        } else {
            snprintf(err, 512, "pkg.low:%u E-PKG-FORM: unknown declaration head `%.*s` — the manifest is a CLOSED set of flat forms: `package <k> <v> .` · `use <n> from \"<s>\" [hash \"<h>\"] .` · `build <k> <v> .`", t[i].line, (int)(t[i].lex.size < 32 ? t[i].lex.size : 32), (const char *)t[i].lex.ptr);
            return false;
        }
        i = e + 1;
    }
    return true;
}
