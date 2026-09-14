// lowentc — **Lowent 컴파일러**의 CLI.
//
// ★ 이름이 **화석이었다**: `lowmini` 는 옛 **트리워킹 인터프리터**(그리고 그것이 돌리던
//   *두 번째, 문서화되지 않은 언어*)의 이름이다. 그 인터프리터는 삭제됐고 — 아래 사용법이
//   그렇게 말하고 있었다 — 그런데 **산물은 계속 그 이름을 달고 있었다.**
//   이름이 실체와 다르면 그것은 검사되지 않는 중복이고, 검사되지 않는 중복은 썩는다(PRINCIPLES.md §0).
//
// 지금 이 도구가 하는 일: 렉스 → 점-닫개 CST → **arity 정규화(나무)** → 단형화 →
//   검사(계약·효과·소유·가시성·계층·능력) → 스택 IR → **① VM 실행**  **② C 방출(네이티브)**.
//   그 둘이 같은 답을 내는지는 차등 스윕이 매번 대조한다.
//
//   lowentc [-t] [--cst] <file.low>
//     (default)  lex → parse → evaluate
//     --cst      dump the CST and stop
//     -t         also dump the token stream
#define _POSIX_C_SOURCE 200809L   // readlink(2) — -std=c23 은 POSIX 선언을 숨긴다
#define _DEFAULT_SOURCE           // realpath(3) — 같은 이유(std 모듈끼리의 자기-가림 판정)
#include "low_hwm.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>

#include "proven/heap.h"
#include "proven/arena.h"
#include "proven/version.h"
#include "low_lex.h"
#include "low_repair.h"
#include "low_cst.h"
#include "low_blake3.h"
#include "low_check.h"
#include "low_typecheck.h"
#include "low_contract.h"
#include "low_region.h"
#include "low_doc.h"
#include "low_version.h"
#include "low_ir.h"
#include "low_cbe.h"
#include "low_pkg.h"
#include <unistd.h>
#include <sys/stat.h>

static bool read_file(const char *path, proven_byte_t **out_buf, proven_size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return false; }
    rewind(f);
    proven_byte_t *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return false; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    *out_buf = buf; *out_len = got;
    return true;
}

static void dump_tokens(const low_lex_result_t *lex) {
    printf("== tokens (%zu) ==\n", (size_t)lex->tokens.len);
    for (proven_size_t i = 0; i < lex->tokens.len; i++) {
        const low_token_t *t = PROVEN_ARRAY_GET(&lex->tokens, low_token_t, i);
        printf("%3u:%-3u %-8s", t->line, t->col, low_tok_kind_name(t->kind));
        if (t->kw) printf(" kw#%d", (int)t->kw);
        // ★ 렉심은 **비어 있어도 찍는다**(`''`). `if (size)` 로 생략하니 빈 문자열 리터럴 `""` 이
        //   렉심 없는 줄이 됐고, check-regexlex 의 줄 파서가 그 줄을 **조용히 버렸다** — 두 렉서가
        //   같은데 대조기가 발산을 보고했다. 빈 문자열을 쓴 첫 픽스처(vm_str)가 드러낸 잠복 결함.
        //   토큰이 있으면 렉심 자리도 있어야 한다 — 없음과 빈 것은 다르다.
        fputs("  '", stdout); fwrite(t->lex.ptr, 1, t->lex.size, stdout); putchar('\'');
        putchar('\n');
    }
}
// ★★ 진단은 **stderr** 로 간다. stdout 이 아니다.
//   `--emit-c` 는 **stdout 으로 C 소스를 낸다.** 그런데 진단도 stdout 으로 나가고 있어서,
//   경고 하나만 있어도 생성된 파일이 **오염**됐다:
//
//       $ lowentc --emit-c prog.low > prog.c && cc prog.c
//       prog.c:1:1: error: expected identifier before '==' token
//           == parse diagnostics (3) ==
//
//   즉 **경고가 하나라도 있는 프로그램은 네이티브로 빌드할 수가 없었다.**
//   (그리고 이번 세션에 W-NOT-YET 고지를 여럿 추가했으니 — 그만큼 더 많이 깨졌다.)
//   자동 차등 훑기를 쓰다 드러났다: **도구를 도구로 쓰면 도구의 결함이 보인다.**
// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **주 사용자가 AI 인데 출력이 산문이었다** (`--diag-json` · REQ-0003)
//
//   안정 코드는 **이미 있었다**(`E-EFFECT-NO-CAP`·`E-VM-BOUNDS` …). 없던 것은 그것을
//   **파싱 없이 읽을 형식**이다. 지금까지 도구가 이 컴파일러를 쓰려면 한국어/영어 산문을
//   정규식으로 뜯어야 했고, 그러면 **메시지 문구가 바뀔 때마다 도구가 깨진다.**
//
//   `--diag-json` 은 진단 하나를 JSON 한 줄로 낸다(JSONL — 스트리밍 가능):
//       {"sev":"error","phase":"parse","code":"E-DOT-DOUBLE","line":3,"col":7,"msg":"…"}
//   산문 렌더는 `msg` 에 그대로 남는다 — 사람이 읽을 것을 뺏지 않는다. **코드가 계약이고
//   msg 는 편의**라는 것이 이 형식의 요점이다.
//
//   **JSONL 은 stderr 로 간다.** stdout 은 `--emit-c` 가 C 를 찍는 곳이라 건드리지 않는다.
//   `--check` 의 판정도 `{"result":…,"exit":…}` 한 줄로 stderr 에 함께 낸다 — 진단만
//   구조화하고 결론은 산문에서 긁게 두면 그 절반이 도구를 깨뜨린다.
//
//   ⚠ 정직: `low_diag_t` 는 **파일 식별자를 안 갖는다**(line/col 뿐). 다중 파일 입력에서
//   어느 파일인지는 산문 출력에서도 이미 없다 — 여기서 지어내지 않고 그대로 비운다.
//   ★ **repair id 가 붙는다** (REQ-0003 닫힘 · 후속 I, 2026-08-04). 코드가 *"무엇이 틀렸나"* 를,
//   수리 id 가 *"무엇을 하면 되나"* 를 말한다 — 둘 다 안정 식별자이고 산문 파싱이 필요 없다.
//   출처는 `low_repair.c` **하나**이고, 방출 자리가 채운 값이 있으면 **그쪽이 이긴다**.
//   ⚠ 정직: 코드만으로 수리가 정해지지 않는 자리는 **비운다**(레지스트리의 NOREPAIR 목록에
//   이유가 적혀 있다). 지어낸 수리는 없는 수리보다 나쁘다 — 읽는 쪽이 그대로 실행한다.
static bool g_diag_json = false;

static void json_str(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\n': fputs("\\n", f);  break;
            case '\r': fputs("\\r", f);  break;
            case '\t': fputs("\\t", f);  break;
            default:
                if (*p < 0x20) fprintf(f, "\\u%04x", *p);
                else           fputc((int)*p, f);
        }
    }
    fputc('"', f);
}

static const char *sev_name(low_severity_t s) {
    switch (s) {
        case LOW_SEV_ERROR:   return "error";
        case LOW_SEV_WARNING: return "warning";
        default:              return "note";
    }
}

// ★★★ **설명은 한 모양으로 나온다** (RFC-0089 R5 · 단계 E, 2026-08-03).
//
//   도구는 *"왜"* 를 **세 곳**에서 말해 왔다: 진단(`--diag-json`) · 하강 이유(`--why-slow`) ·
//   방출 자격의 이유(R1 의 `plan.why`). 셋이 서로 다른 모양이면 그것을 읽는 쪽(CLI·IDE·AI)이
//   **세 번 파싱**해야 하고, 한 곳의 문구가 바뀌면 그 도구가 깨진다. 새 정보를 만드는 일이
//   아니라 **이미 아는 것의 출구를 하나로** 만드는 일이다.
//
//       {"rule":…, "sev":…, "phase":…, "span":{"line":L,"col":C},
//        "promise":…, "fact":…, "relation":…, "msg":…, "repair":…}
//
//   · rule     — **안정 식별자**(E-…/W-…). 이것이 계약이고 `msg` 는 편의다.
//   · span     — 어디인가. 모르는 자리는 0 이다(지어내지 않는다).
//   · promise  — 무엇이 약속됐는가(규칙이 말하는 것).
//   · fact     — 무엇이 실제인가(도구가 잰 것).
//   · relation — 그 둘의 관계: violates(어겼다) · unmet(못 지켰다) · absent(없다).
//
//   ⚠ **정직**: 보통의 진단에는 `promise`/`fact` 를 **안 붙인다.** 지금 도구가 그 둘을 따로
//     아는 자리는 하강 이유와 방출 자격뿐이고, 나머지에 대해 산문을 쪼개 지어내면 그것은
//     구조가 아니라 **꾸며낸 구조**다. 아는 자리에만 적고, 모르는 자리는 비운다.
static void expl_json(const char *rule, const char *sev, const char *phase,
                      const char *file, unsigned line, unsigned col, const char *promise,
                      const char *fact, const char *relation, const char *msg,
                      const char *repair) {
    fputs("{\"rule\":", stderr);        json_str(stderr, rule ? rule : "");
    fputs(",\"sev\":", stderr);         json_str(stderr, sev);
    fputs(",\"phase\":", stderr);       json_str(stderr, phase);
    if (file) { fputs(",\"file\":", stderr); json_str(stderr, file); }   // ★ 2026-09-14 — 파일을 아는 진단은 파일도 싣는다
    fprintf(stderr, ",\"span\":{\"line\":%u,\"col\":%u}", line, col);
    if (promise) { fputs(",\"promise\":", stderr); json_str(stderr, promise); }
    if (fact)    { fputs(",\"fact\":", stderr);    json_str(stderr, fact); }
    fputs(",\"relation\":", stderr);    json_str(stderr, relation);
    fputs(",\"msg\":", stderr);         json_str(stderr, msg ? msg : "");
    if (repair)  { fputs(",\"repair\":", stderr);  json_str(stderr, repair); }
    fputs("}\n", stderr);
}

// ★ 한 파일에서 온 나무 전체에 그 파일 경로를 새긴다 (단계 V).
static void cst_stamp_file(low_cst_t *nd, const char *file) {
    if (!nd) return;
    nd->file = file;
    for (proven_size_t i = 0; i < nd->nkids; i++) cst_stamp_file(nd->kids[i], file);
}

static void dump_diags(const char *what, const proven_array_t *diags) {
    if (!diags->len) return;
    if (g_diag_json) {
        // phase = `what` 의 첫 낱말 ("parse diagnostics" → "parse").
        char phase[32]; size_t n = 0;
        for (const char *p = what; *p && *p != ' ' && n + 1 < sizeof phase; p++) phase[n++] = *p;
        phase[n] = '\0';
        for (proven_size_t i = 0; i < diags->len; i++) {
            const low_diag_t *d = PROVEN_ARRAY_GET(diags, low_diag_t, i);
            // ★ 규칙을 어긴 것이 진단이다 — relation 은 언제나 `violates`.
            // ★ 수리는 **한 곳에서** 온다: 방출 자리가 채웠으면 그것, 아니면 레지스트리.
            //   (`low_repair.c` — 코드가 수리를 결정하는 자리만 표에 있다.)
            expl_json(d->code, sev_name(d->sev), phase, d->file, d->line, d->col,
                      NULL, NULL, "violates", low_diag_text(d),
                      d->repair ? d->repair : low_repair_for(d->code));
        }
        return;
    }
    fprintf(stderr, "== %s (%zu) ==\n", what, (size_t)diags->len);
    for (proven_size_t i = 0; i < diags->len; i++) {
        const low_diag_t *d = PROVEN_ARRAY_GET(diags, low_diag_t, i);
        // ★ 파일을 아는 진단은 **파일부터** 말한다 — 한 단위가 여러 파일이고 줄은 파일마다
        //   1 부터 다시 시작하기 때문이다(단계 V). 모르면 여태처럼 줄만 말한다: **없는 것을
        //   지어내지 않는다.**
        if (d->file) fprintf(stderr, "  %s:%u:%u %s: %s\n", d->file, d->line, d->col, d->code, low_diag_text(d));
        else         fprintf(stderr, "  %u:%u %s: %s\n", d->line, d->col, d->code, low_diag_text(d));
    }
}


// ═══════════════════════════════════════════════════════════════════════════
// ★★★ **의존은 이름이 아니라 내용이다** (RFC-0032 D3/D4 · RFC-0023 내용주소화)
//
//   `use math from "lib/math.low" .` — `from` 이 **어디서 오는가**를 말한다.
//   그리고 락파일(`lowent.lock`)이 **그것이 무엇인가**를 말한다: **내용의 해시**.
//
//   ★ 요점은 이것이다: **미러를 신뢰하지 않아도 무결하다.** 어디서 받았든 —
//     공개 서버든, 사설 서버든, 남의 캐시든 — **해시가 같으면 같은 패키지다.**
//     Go 의 sumdb 가 푸는 문제를 내용주소화가 **구조적으로** 없앤다.
//
//   ★★ 그리고 해시가 다르면 **거절한다**. 조용히 새 버전을 쓰는 것이 바로 공급망 사고다.
typedef struct { char name[64]; char hex[65]; } lock_ent_t;
static lock_ent_t g_lock[64];
static int        g_nlock = 0;
static bool       g_have_lock = false;
static const char *g_pkg_path = NULL;   // --pkg <file> — 매니페스트 위치를 명시(발견 걷기 생략)
static FILE       *g_lock_out = NULL;   // --lock-write 가 pkg.lock **파일**에도 쓴다 (매니페스트가 있을 때)

static void lock_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open lockfile: %s\n", path); exit(2); }
    g_have_lock = true;
    char ln[256];
    while (fgets(ln, sizeof ln, f) && g_nlock < 64) {
        if (ln[0] == '#' || ln[0] == '\n') continue;
        if (sscanf(ln, "%63s %64s", g_lock[g_nlock].name, g_lock[g_nlock].hex) == 2) g_nlock++;
    }
    fclose(f);
}

static void hash_hex(const proven_byte_t *b, proven_size_t n, char out[65]) {
    proven_u8 h[32];
    low_blake3_256(b, n, h);
    for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", h[i]);
    out[64] = 0;
}

// 의존을 **검증한다**. 락파일이 있으면: 없는 항목 = 거절 · 다른 해시 = 거절.
static bool dep_verify(const char *name, const char *path,
                       const proven_byte_t *buf, proven_size_t len, bool write_lock) {
    char hex[65];
    hash_hex(buf, len, hex);
    if (write_lock) {
        printf("%s %s\n", name, hex);
        if (g_lock_out) fprintf(g_lock_out, "%s %s\n", name, hex);
        return true;
    }
    if (!g_have_lock) return true;   // 락파일을 안 줬으면 고정할 것이 없다(그리고 그렇게 말하지 않는다)
    for (int i = 0; i < g_nlock; i++) {
        if (strcmp(g_lock[i].name, name) != 0) continue;
        if (strcmp(g_lock[i].hex, hex) == 0) return true;
        fprintf(stderr, "E-DEP-HASH: dependency `%s` (%s) does NOT match the lockfile.\n"
                "  locked  %s\n  actual  %s\n"
                "  The content changed. That is the whole point of pinning content and not names:\n"
                "  you do not have to trust WHERE it came from — only WHAT it is. A mirror that\n"
                "  serves different bytes under the same name is exactly the supply-chain attack\n"
                "  this refusal exists to stop.\n", name, path, g_lock[i].hex, hex);
        return false;
    }
    fprintf(stderr, "E-DEP-UNLOCKED: dependency `%s` (%s) has no entry in the lockfile.\n"
            "  An unpinned dependency is whatever the server felt like sending TODAY.\n"
            "  Run with --lock-write to record what you are actually building against.\n", name, path);
    return false;
}

// `use <name> from "<path>"` 를 **토큰에서** 읽는다 (문자열 스캔이 아니라 렉서에게 묻는다).
// ★ from-생략 `use <name> .` / `use <name> as <y> .` 도 잡는다 — paths_out = "" 로 표시하고
//   해소(예약 std → 매니페스트 → 형제 dir, RFC-0011 §6.3)는 호출자가 한다.
// ★★★★ **입력 파일이 조용히 사라졌다** (2026-09-09 · REQ-0012). 이 훑기는 `found == max` 에서
//   멈추기만 했다 — 열일곱 번째 `use … from` 은 **로드되지 않았고**, 그 파일의 틀린 몸통은
//   검사되지 않은 채 `--check` 가 초록으로 끝났다(경고 `W-USE-EXTERNAL` 하나만 남았다).
//   ⇒ 몇 개를 담았는지와 **몇 개가 있었는지**를 함께 말한다. 자르는 자리는 자른다고 말해야 한다.
//   ☞ *«못 담았다» 를 말하지 않는 자르기는 «없었다» 와 구별되지 않는다.*
#define MAX_DEPS_PER_FILE 32
static int deps_of(proven_allocator_t heap, const proven_byte_t *buf, proven_size_t len,
                   char names[][64], char paths_out[][256], int max, int *total) {
    proven_u8str_view_t sv = { .ptr = buf, .size = len };
    low_lex_result_t lx = low_lex(heap, sv);
    const low_token_t *t = (const low_token_t *)lx.tokens.data;
    proven_size_t n = lx.tokens.len;
    int found = 0;
    if (total) *total = 0;
    for (proven_size_t i = 0; i + 2 < n; i++) {
        if (t[i].kw != LOW_KW_USE) continue;
        if (t[i + 1].kind != LOW_TOK_IDENT) continue;
        proven_size_t nn = t[i + 1].lex.size < 63 ? t[i + 1].lex.size : 63;
        if (i + 3 < n && t[i + 2].kind == LOW_TOK_IDENT &&
            proven_u8str_view_eq(t[i + 2].lex, proven_u8str_view_from_cstr("from")) &&
            t[i + 3].kind == LOW_TOK_STRING) {
            if (total) (*total)++;
            if (found >= max) continue;              // 담지는 못해도 **세기는 한다**
            memcpy(names[found], t[i + 1].lex.ptr, nn); names[found][nn] = 0;
            proven_size_t pn = t[i + 3].lex.size < 255 ? t[i + 3].lex.size : 255;
            memcpy(paths_out[found], t[i + 3].lex.ptr, pn); paths_out[found][pn] = 0;
            found++;
        } else if (t[i + 2].kind == LOW_TOK_DOT ||
                   (t[i + 2].kind == LOW_TOK_IDENT &&
                    proven_u8str_view_eq(t[i + 2].lex, proven_u8str_view_from_cstr("as")))) {
            if (total) (*total)++;
            if (found >= max) continue;
            memcpy(names[found], t[i + 1].lex.ptr, nn); names[found][nn] = 0;
            paths_out[found][0] = 0;   // from-생략 — 3계층 해소 대상
            found++;
        }
    }
    return found;
}

// ── from-생략 1계층: **예약(표준) 모듈** — 도구 설치 dir 의 닫힌 집합 (RFC-0011 §6.3 ①) ──
//
// ★ 이름은 **파일 안의 `module` 선언**이다, 파일 이름이 아니다(lib/vec.low 의 모듈은 vecs).
//   그래서 std dir 을 한 번 훑어 이름→파일 지도를 만든다. ambient 환경변수 재지정은 없다
//   (2D: 예약 이름 해소의 뿌리는 **설치 자체**다 — 실행파일 기준 상대 경로).
#include <dirent.h>
typedef struct { char name[64]; char path[512]; } std_mod_t;
static std_mod_t g_std[64];
static int       g_nstd = -1;   // -1 = 아직 안 훑음

// 소스 바이트에서 `module <name> .` 을 읽는다 — std_scan_dir 과 **같은 규칙**(한 어휘, 한 자리).
static bool mod_name_of(const proven_byte_t *b, proven_size_t n, char *out, size_t cap) {
    if (!b || !n) return false;
    for (proven_size_t i = 0; i < n; ) {
        while (i < n && (b[i] == ' ' || b[i] == '\n' || b[i] == '\t' || b[i] == '\r')) i++;
        if (i + 7 <= n && memcmp(b + i, "module ", 7) == 0) {
            i += 7;
            proven_size_t s = i;
            while (i < n && b[i] != ' ' && b[i] != '\n' && b[i] != '.') i++;
            size_t ln = (size_t)(i - s);
            if (!ln || ln >= cap) return false;
            memcpy(out, b + s, ln); out[ln] = 0;
            return true;
        }
        while (i < n && b[i] != '\n') i++;
        if (i < n) i++;
    }
    return false;
}

static void std_scan_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && g_nstd < 64) {
        size_t ln = strlen(e->d_name);
        if (ln < 5 || strcmp(e->d_name + ln - 4, ".low") != 0) continue;
        char fp[600]; snprintf(fp, sizeof fp, "%s/%s", dir, e->d_name);
        FILE *f = fopen(fp, "rb");
        if (!f) continue;
        // 줄 머리의 `module <name> .` 을 찾는다 (rem 줄은 건너뛴다).
        // ★★★ **파일 전체를 훑는다** (2026-09-04, 라이브러리 보안 검토).
        //   전에는 **앞 4096 바이트만** 읽었다. 머리말이 그보다 길면 `module` 선언을 못 보고,
        //   그 모듈은 표준 색인에서 **조용히 사라졌다**. 그러면 `use <이름> .`(경로 생략)이
        //   해소되지 않고 부르는 쪽에는 `E-IR-UNDEF`(undefined name) 만 남는다 — 도구가
        //   **호출자를 탓한다**(교훈 5: 틀린 것은 도구인데 네 프로그램이 틀렸다고 말한다).
        //   실측: `lib/pool.low` 은 머리말이 7458 바이트라 **이미 사라져 있었다**(아무도 못 봤다 —
        //   픽스처가 모두 `from "…"` 경로 형태로 불렀기 때문이다). `lib/regex.low` 은 머리말이
        //   한 줄 늘자 4096 을 넘어 그날로 사라졌고, 그제야 픽스처가 빨간불이 되어 보였다.
        //   ⇒ 한 줄씩 읽어 **길이와 무관하게** 찾는다. 파일당 한 번, 시작할 때뿐이다.
        char *line = NULL; size_t lcap = 0; ssize_t lgot;
        while ((lgot = getline(&line, &lcap, f)) > 0) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "module ", 7) != 0) continue;
            p += 7;
            char *q = p; while (*q && *q != ' ' && *q != '\n' && *q != '\r' && *q != '.') q++;
            size_t mn = (size_t)(q - p);
            if (mn > 0 && mn < 64) {
                snprintf(g_std[g_nstd].name, 64, "%.*s", (int)mn, p);
                snprintf(g_std[g_nstd].path, 512, "%s", fp);
                g_nstd++;
            }
            break;
        }
        free(line);
        fclose(f);
    }
    closedir(d);
}

static void std_scan(const char *argv0) {
    if (g_nstd >= 0) return;
    g_nstd = 0;
    char exe[512] = { 0 };
    ssize_t r = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (r <= 0) snprintf(exe, sizeof exe, "%s", argv0);
    char *sl = strrchr(exe, '/');
    if (sl) *sl = 0; else snprintf(exe, sizeof exe, ".");
    char d1[600], d2[600];
    snprintf(d1, sizeof d1, "%s/../std", exe);        // 설치 레이아웃 <install>/{bin,std}
    snprintf(d2, sizeof d2, "%s/../../lib", exe);     // 개발 트리 impl/build → lowent_lang/lib
    std_scan_dir(d1);
    std_scan_dir(d2);
    // ★ 사용자 스코프 (RFC-0033 D4) — 관리자 아니어도 설치·해소가 된다. 전역(std)이 먼저
    //   훑리므로 사용자 lib 은 예약 이름을 **가릴 수 없다**(std_find 는 첫 항목이 이긴다).
    const char *home = getenv("HOME");
    if (home) {
        char du[600];
        snprintf(du, sizeof du, "%s/.lowent/lib", home);
        std_scan_dir(du);
    }
}

static const char *std_find(const char *name) {
    for (int i = 0; i < g_nstd; i++)
        if (strcmp(g_std[i].name, name) == 0) return g_std[i].path;
    return NULL;
}

// ═══════════════════════════════════════════════════════════════════════════
// ★★ **서브커맨드 — 한 바이너리** (RFC-0033 D6). `lowentc check f.low` = `--check f.low`.
//   빌드 도구를 따로 만들지 않는다: 컴파일러가 이미 아는 것(의존·해시·링크 플래그)을
//   두 번째 도구에 다시 가르치면 그것이 곧 갈라지는 두 번째 표현이다(교훈 7).
static const char *sub_flag(const char *w) {
    if (strcmp(w, "check") == 0) return "--check";
    if (strcmp(w, "test") == 0)  return "--test";
    if (strcmp(w, "doc") == 0)   return "--doc";
    if (strcmp(w, "fmt") == 0)   return "--fmt";
    if (strcmp(w, "ir") == 0)    return "--ir";
    if (strcmp(w, "lock") == 0)  return "--lock-write";
    return NULL;
}

static bool file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) { fclose(f); return true; } return false; }

// 엔트리를 정한다: 명시 인자 > 매니페스트 `build entry` > 관례(src/main.low → main.low, RFC-0033 D9).
static bool resolve_entry(const char *arg, low_pkg_t *pkg, char out[600], char err[256]) {
    if (arg) { snprintf(out, 600, "%s", arg); return true; }
    if (low_pkg_find(".", pkg)) {
        proven_allocator_t heap = proven_heap_allocator();
        char perr[512];
        if (!low_pkg_load(heap, pkg, perr)) { snprintf(err, 256, "%s", perr); return false; }
        if (pkg->entry[0]) {
            snprintf(out, 600, "%s/%s", pkg->dir, pkg->entry);
            if (file_exists(out)) return true;
            snprintf(err, 256, "manifest names entry \"%s\" but %s does not exist", pkg->entry, out);
            return false;
        }
        char c1[600]; snprintf(c1, sizeof c1, "%s/src/main.low", pkg->dir);
        if (file_exists(c1)) { snprintf(out, 600, "%s", c1); return true; }
        snprintf(c1, sizeof c1, "%s/main.low", pkg->dir);
        if (file_exists(c1)) { snprintf(out, 600, "%s", c1); return true; }
        snprintf(err, 256, "no entry: give a file, or add `build entry \"<file>\" .` to %s, or use the src/main.low convention", pkg->path);
        return false;
    }
    if (file_exists("src/main.low")) { snprintf(out, 600, "src/main.low"); return true; }
    if (file_exists("main.low")) { snprintf(out, 600, "main.low"); return true; }
    snprintf(err, 256, "no entry: give a .low file, or run inside a project (pkg.low / src/main.low)");
    return false;
}

// `lowentc build [entry.low]` — C 방출 → cc → 실행 파일. 도구가 이미 아는 것만 잇는다:
//   emit-c(코드) · emit-ld(링크 플래그, 손으로 -lm 을 37곳에 적던 그 교훈) · 매니페스트(이름).
static int cmd_build(int nargs, char **args) {
    // ★ 크로스빌드 손잡이(RFC-0031)는 **컴파일러에게 그대로 넘긴다** — 도구가 두 번째로
    //   해석하면 그것이 곧 갈라지는 두 번째 표현이다. build 는 잇기만 한다.
    const char *arg = NULL;
    char pass[1024] = { 0 };
    for (int i = 0; i < nargs; i++) {
        if ((strcmp(args[i], "--target") == 0 || strcmp(args[i], "--config") == 0) && i + 1 < nargs) {
            size_t n = strlen(pass);
            snprintf(pass + n, sizeof pass - n, " %s %s", args[i], args[i + 1]);
            i++;
        } else if (strcmp(args[i], "--profile") == 0 && i + 1 < nargs) {
            // ★ profile 은 **소스가 선언한다**(`build profile <이름> .`) — CLI 플래그가 아니다.
            //   여기서 조용히 삼키면 "지정했는데 안 먹었다" 가 되고, 몰래 지어내 넘기면 컴파일러가
            //   모르는 낱말을 받는다. ⇒ 어느 쪽도 아니라고 **말한다**.
            fprintf(stderr, "lowentc build: `--profile` is not a compiler flag — the PROFILE is declared "
                            "in the source (`build profile %s .`) or in pkg.low, so that what you build "
                            "is written down, not passed on a command line (RFC-0031)\n", args[i + 1]);
            return 2;
        } else if (strncmp(args[i], "--", 2) == 0) {
            size_t n = strlen(pass);
            snprintf(pass + n, sizeof pass - n, " %s", args[i]);
        } else if (!arg) arg = args[i];
    }
    low_pkg_t pkg = { 0 };
    char entry[600], err[256];
    if (!resolve_entry(arg, &pkg, entry, err)) { fprintf(stderr, "lowentc build: %s\n", err); return 2; }
    // 매니페스트의 target 은 CLI 가 없을 때만 쓴다(설정 우선순위 그대로).
    // (profile 은 소스 선언이라 여기서 넘기지 않는다 — 위 참조.)
    if (pkg.found && !strstr(pass, "--target") && pkg.target[0]) {
        size_t n = strlen(pass);
        snprintf(pass + n, sizeof pass - n, " --target %s", pkg.target);
    }
    char self[512] = { 0 };
    ssize_t r = readlink("/proc/self/exe", self, sizeof self - 1);
    if (r <= 0) snprintf(self, sizeof self, "lowentc");
    const char *name = pkg.found && pkg.name[0] ? pkg.name : NULL;
    char nb[128];
    if (!name) {
        const char *sl = strrchr(entry, '/');
        snprintf(nb, sizeof nb, "%s", sl ? sl + 1 : entry);
        char *dot = strrchr(nb, '.');
        if (dot) *dot = 0;
        name = nb;
    }
    char outdir[600];
    if (pkg.found) { snprintf(outdir, sizeof outdir, "%s/out", pkg.dir); mkdir(outdir, 0755); }
    else snprintf(outdir, sizeof outdir, ".");
    char cfile[736], bin[736], cmd[4096], ld[512] = { 0 };
    snprintf(cfile, sizeof cfile, "%s/.%s.c", outdir, name);
    snprintf(bin, sizeof bin, "%s/%s", outdir, name);
    snprintf(cmd, sizeof cmd, "\"%s\"%s --emit-c \"%s\" > \"%s\"", self, pass, entry, cfile);
    if (system(cmd) != 0) { fprintf(stderr, "lowentc build: emit failed for %s\n", entry); return 1; }
    snprintf(cmd, sizeof cmd, "\"%s\"%s --emit-ld \"%s\"", self, pass, entry);
    FILE *lp = popen(cmd, "r");
    if (lp) { if (!fgets(ld, sizeof ld, lp)) ld[0] = 0; pclose(lp); }
    char *nl = strchr(ld, '\n'); if (nl) *nl = 0;
    // ★★★ **증분은 내용주소다** (RFC-0031 · RFC-0012). 타임스탬프가 아니라 **방출된 C 의
    //   해시**(+링크 플래그)가 열쇠다: 소스가 바뀌어도 C 가 같으면(주석·순서만 달라졌으면)
    //   같은 산물이고, 시계가 거꾸로 가도 틀리지 않는다. make 의 mtime 이 못 하는 두 가지다.
    proven_byte_t *cb = NULL; proven_size_t cn = 0;
    char key[65] = { 0 };
    if (read_file(cfile, &cb, &cn)) {
        // 링크 플래그도 정체의 일부다 — 같은 C 를 다른 라이브러리로 링크하면 다른 산물이다.
        proven_byte_t *mix = (proven_byte_t *)malloc(cn + sizeof ld);
        if (mix) {
            memcpy(mix, cb, cn);
            memcpy(mix + cn, ld, sizeof ld);
            hash_hex(mix, cn + sizeof ld, key);
            free(mix);
        }
        free(cb);
    }
    const char *home = getenv("HOME");
    char cache[736] = { 0 };
    if (key[0] && home) {
        char base[600];
        snprintf(base, sizeof base, "%s/.lowent", home); mkdir(base, 0755);
        snprintf(base, sizeof base, "%s/.lowent/cache", home); mkdir(base, 0755);
        snprintf(base, sizeof base, "%s/.lowent/cache/build", home); mkdir(base, 0755);
        snprintf(cache, sizeof cache, "%s/.lowent/cache/build/%s", home, key);
        if (file_exists(cache)) {
            snprintf(cmd, sizeof cmd, "cp \"%s\" \"%s\" && chmod +x \"%s\"", cache, bin, bin);
            if (system(cmd) == 0) {
                remove(cfile);
                printf("built %s (cache hit %.12s…)\n", bin, key);
                return 0;
            }
        }
    }
    snprintf(cmd, sizeof cmd, "cc -O2 -w \"%s\" -o \"%s\" %s -lm", cfile, bin, ld);
    if (system(cmd) != 0) { fprintf(stderr, "lowentc build: cc failed (%s)\n", cfile); return 1; }
    remove(cfile);
    if (cache[0]) {
        snprintf(cmd, sizeof cmd, "cp \"%s\" \"%s\"", bin, cache);
        (void)system(cmd);
    }
    printf("built %s\n", bin);
    return 0;
}

// `lowentc add <name> <source>` — 의존을 매니페스트에 **해시 핀과 함께** 적는다.
//   적는 순간이 곧 고정하는 순간이다: 핀 없는 추가는 "오늘 서버가 준 것"을 믿는 것이다.
static int cmd_add(const char *name, const char *source) {
    if (!name || !source) { fprintf(stderr, "usage: lowentc add <name> <source-path>\n"); return 2; }
    low_pkg_t pkg = { 0 };
    if (!low_pkg_find(".", &pkg)) {
        fprintf(stderr, "lowentc add: no pkg.low found — a dependency needs a manifest to live in. "
                        "Create pkg.low first (package name/version)\n");
        return 2;
    }
    proven_allocator_t heap = proven_heap_allocator();
    char perr[512];
    if (!low_pkg_load(heap, &pkg, perr)) { fprintf(stderr, "%s\n", perr); return 1; }
    if (low_pkg_dep_find(&pkg, name)) {
        fprintf(stderr, "lowentc add: `%s` is already declared in %s — two declarations for one "
                        "name means every reader must pick (E-PKG-DUP). Edit the existing line\n",
                name, pkg.path);
        return 1;
    }
    char sp[600];
    if (source[0] == '/') snprintf(sp, sizeof sp, "%s", source);
    else snprintf(sp, sizeof sp, "%s/%s", pkg.dir, source);
    proven_byte_t *db = NULL; proven_size_t dl = 0;
    if (!read_file(sp, &db, &dl)) {
        fprintf(stderr, "lowentc add: cannot read %s — a dependency the tool cannot see is a "
                        "dependency nobody checked (E-DEP-MISSING). For URLs, vendor the file first\n", sp);
        return 1;
    }
    char hex[65];
    hash_hex(db, dl, hex);
    free(db);
    FILE *f = fopen(pkg.path, "a");
    if (!f) { fprintf(stderr, "lowentc add: cannot write %s\n", pkg.path); return 1; }
    fprintf(f, "use %s from \"%s\" hash \"%s\" .\n", name, source, hex);
    fclose(f);
    printf("added %s from \"%s\" (pinned %.12s…) to %s\n", name, source, hex, pkg.path);
    return 0;
}

// ── **설정** (RFC-0033 D3 · 2D) — 우선순위: CLI > 프로젝트(./lowpm.local) > 사용자(~/.lowent/config)
//    > 전역(<install>/config). 비밀·미러는 **소스 밖**이다(RFC-0032 D5) — 소스는 무엇(이름·해시)만
//    말하고, 어디서·어떻게(미러·토큰)는 기계의 설정이 말한다. 그래서 소스는 공유해도 안전하다.
typedef struct {
    char mirror[8][256]; int nmirror;   // content-addressed 미러: <base>/<hash>.low
    char policy[24];                    // any | local-only | official-only
} low_cfg_t;
static low_cfg_t g_cfg;
static void cfg_read(const char *p) {
    FILE *f = fopen(p, "r");
    if (!f) return;
    char ln[512];
    while (fgets(ln, sizeof ln, f)) {
        char k[32], v[400];
        if (ln[0] == '#' || ln[0] == '\n') continue;
        if (sscanf(ln, "%31s %399s", k, v) != 2) continue;
        if (strcmp(k, "mirror") == 0 && g_cfg.nmirror < 8) snprintf(g_cfg.mirror[g_cfg.nmirror++], 256, "%s", v);
        else if (strcmp(k, "policy") == 0) snprintf(g_cfg.policy, sizeof g_cfg.policy, "%s", v);
    }
    fclose(f);
}
static void cfg_load(void) {
    // 낮은 우선순위부터 읽는다 — 나중 파일이 policy 를 덮는다(mirror 는 앞에서부터 누적·시도 순).
    char exe[512] = { 0 };
    ssize_t r = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (r > 0) {
        char *sl = strrchr(exe, '/');
        if (sl) { *sl = 0; char p[600]; snprintf(p, sizeof p, "%s/../config", exe); cfg_read(p); }
    }
    const char *home = getenv("HOME");
    if (home) { char p[600]; snprintf(p, sizeof p, "%s/.lowent/config", home); cfg_read(p); }
    cfg_read("./lowpm.local");
    if (!g_cfg.policy[0]) snprintf(g_cfg.policy, sizeof g_cfg.policy, "any");
}

// ── **서명 = 진본** (RFC-0032 D7 · 2C ②층) — 해시(③ 무결)와 **다른 질문**이다:
//    해시는 "바이트가 그대로인가", 서명은 "**그 발행자가 낸 것**인가"를 답한다.
//    프리미티브는 ed25519, 실행은 openssl(시스템 도구 폴백 — cc·curl 과 같은 패턴). 없으면 없다고 말한다.
static bool have_openssl(void) { return system("openssl version >/dev/null 2>&1") == 0; }
static bool sig_verify_with(const char *pub, const char *file, const char *sig) {
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "openssl pkeyutl -verify -pubin -inkey \"%s\" -rawin -in \"%s\" -sigfile \"%s\" >/dev/null 2>&1",
             pub, file, sig);
    return system(cmd) == 0;
}
// 신뢰 키(공식 루트) = **폴더의 공개키 파일들** (RFC-0033 D7): ~/.lowent/keys/official/*.pub + <install>/keys/official/*.pub.
// 어느 키가 검증했는지 keyname 에 담는다. 못 하면 false.
static bool sig_verify_trusted(const char *file, const char *sig, char keyname[128]) {
    char dirs[2][600]; int nd = 0;
    const char *home = getenv("HOME");
    if (home) snprintf(dirs[nd++], 600, "%s/.lowent/keys/official", home);
    char exe[512] = { 0 };
    ssize_t r = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (r > 0) { char *sl = strrchr(exe, '/'); if (sl) { *sl = 0; snprintf(dirs[nd++], 600, "%s/../keys/official", exe); } }
    for (int i = 0; i < nd; i++) {
        DIR *d = opendir(dirs[i]);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d))) {
            size_t ln = strlen(e->d_name);
            if (ln < 5 || strcmp(e->d_name + ln - 4, ".pub") != 0) continue;
            char pp[736]; snprintf(pp, sizeof pp, "%s/%s", dirs[i], e->d_name);
            if (sig_verify_with(pp, file, sig)) {
                snprintf(keyname, 128, "%.*s", (int)(ln - 4), e->d_name);
                closedir(d);
                return true;
            }
        }
        closedir(d);
    }
    return false;
}

// `lowentc key new <name>` / `key list` — 키는 **폴더의 파일**이다(RFC-0033 D7): 비밀키는
//   ~/.lowent/keys/private/<n>.pem(0600), 공개키는 ~/.lowent/keys/official/<n>.pub — 제 키는 제
//   신뢰 뿌리에 들어간다(남의 키를 신뢰하는 것은 그 .pub 파일을 그 폴더에 **직접 넣는** 행위다:
//   신뢰가 곧 눈에 보이는 파일이라, 무엇을 믿고 있는지 ls 가 말해 준다).
static int cmd_key(int n, char **a) {
    const char *home = getenv("HOME");
    if (!home) { fprintf(stderr, "lowentc key: no HOME\n"); return 2; }
    char kd[600], pd[600], od[600];
    snprintf(kd, sizeof kd, "%s/.lowent", home); mkdir(kd, 0755);
    snprintf(kd, sizeof kd, "%s/.lowent/keys", home); mkdir(kd, 0755);
    snprintf(pd, sizeof pd, "%s/private", kd); mkdir(pd, 0700);
    snprintf(od, sizeof od, "%s/official", kd); mkdir(od, 0755);
    if (n >= 2 && strcmp(a[0], "new") == 0) {
        if (!have_openssl()) { fprintf(stderr, "lowentc key: openssl not found — the signature layer needs it (same fallback pattern as cc/curl)\n"); return 1; }
        char priv[736], pub[736], cmd[2048];
        snprintf(priv, sizeof priv, "%s/%s.pem", pd, a[1]);
        snprintf(pub, sizeof pub, "%s/%s.pub", od, a[1]);
        if (file_exists(priv)) { fprintf(stderr, "lowentc key: %s already exists — a silently replaced key would orphan every signature it made\n", priv); return 1; }
        snprintf(cmd, sizeof cmd, "openssl genpkey -algorithm ed25519 -out \"%s\" >/dev/null 2>&1 && chmod 600 \"%s\" && openssl pkey -in \"%s\" -pubout -out \"%s\" >/dev/null 2>&1", priv, priv, priv, pub);
        if (system(cmd) != 0) { fprintf(stderr, "lowentc key: openssl keygen failed\n"); return 1; }
        printf("key %s:\n  private  %s  (0600 — never leaves this machine)\n  public   %s  (your own trust root; give THIS file to people who should trust your signatures)\n", a[1], priv, pub);
        return 0;
    }
    if (n >= 1 && strcmp(a[0], "list") == 0) {
        for (int which = 0; which < 2; which++) {
            const char *dd = which ? pd : od;
            printf("%s:\n", which ? "private (yours)" : "official (trusted roots)");
            DIR *d = opendir(dd);
            if (d) { struct dirent *e; int c = 0;
                while ((e = readdir(d))) if (e->d_name[0] != '.') { printf("  %s\n", e->d_name); c++; }
                closedir(d);
                if (!c) printf("  (none)\n");
            } else printf("  (none)\n");
        }
        return 0;
    }
    fprintf(stderr, "usage: lowentc key new <name> | key list\n");
    return 2;
}

// `lowentc sign <file> [--key <name>]` — 분리 서명 `<file>.sig` (ed25519, 바이트 전체).
static int cmd_sign(int n, char **a) {
    const char *file = NULL, *key = "default";
    for (int i = 0; i < n; i++) {
        if (strcmp(a[i], "--key") == 0 && i + 1 < n) key = a[++i];
        else if (!file) file = a[i];
    }
    if (!file) { fprintf(stderr, "usage: lowentc sign <file> [--key <name>]\n"); return 2; }
    const char *home = getenv("HOME");
    if (!home) { fprintf(stderr, "lowentc sign: no HOME\n"); return 2; }
    if (!have_openssl()) { fprintf(stderr, "lowentc sign: openssl not found\n"); return 1; }
    char priv[736], sig[736], cmd[3072];
    snprintf(priv, sizeof priv, "%s/.lowent/keys/private/%s.pem", home, key);
    if (!file_exists(priv)) { fprintf(stderr, "lowentc sign: no key %s — make one: lowentc key new %s\n", priv, key); return 1; }
    snprintf(sig, sizeof sig, "%s.sig", file);
    snprintf(cmd, sizeof cmd, "openssl pkeyutl -sign -inkey \"%s\" -rawin -in \"%s\" -out \"%s\" 2>/dev/null", priv, file, sig);
    if (system(cmd) != 0) { fprintf(stderr, "lowentc sign: signing failed (readable file? valid key?)\n"); return 1; }
    printf("signed %s → %s (key %s)\n", file, sig, key);
    return 0;
}

// `lowentc verify <file>` — 4층을 **분리해서** 답한다(RFC-0032 D7 · 2C). 섞으면 아무것도 답이 아니다.
static int cmd_verify(int n, char **a) {
    if (n < 1) { fprintf(stderr, "usage: lowentc verify <file>\n"); return 2; }
    const char *file = a[0];
    proven_byte_t *b = NULL; proven_size_t ln = 0;
    if (!read_file(file, &b, &ln)) { fprintf(stderr, "lowentc verify: cannot read %s\n", file); return 1; }
    char hex[65];
    hash_hex(b, ln, hex);
    free(b);
    int rc = 0;
    printf("verify %s\n", file);
    printf("  [3 integrity]     BLAKE3 %s\n", hex);
    // 락/핀과 대조 — 이 내용을 무엇이 알고 있는가
    low_pkg_t pkg = { 0 };
    bool pinned = false;
    if (low_pkg_find(".", &pkg)) {
        proven_allocator_t heap = proven_heap_allocator();
        char perr[512];
        if (low_pkg_load(heap, &pkg, perr)) {
            for (int i = 0; i < pkg.ndeps; i++)
                if (pkg.deps[i].hash[0] && strcmp(pkg.deps[i].hash, hex) == 0) {
                    printf("                    = manifest pin `%s` (%s)\n", pkg.deps[i].name, pkg.path);
                    pinned = true;
                }
            char lp[700]; snprintf(lp, sizeof lp, "%s/pkg.lock", pkg.dir);
            FILE *lf = fopen(lp, "r");
            if (lf) {
                char l2[256], nm[64], hx[80];
                while (fgets(l2, sizeof l2, lf))
                    if (l2[0] != '#' && sscanf(l2, "%63s %79s", nm, hx) == 2 && strcmp(hx, hex) == 0) {
                        printf("                    = pkg.lock entry `%s`\n", nm);
                        pinned = true;
                    }
                fclose(lf);
            }
        }
    }
    if (!pinned) printf("                    (no manifest pin / lock entry matches this content)\n");
    // ② 진본 — 분리 서명
    char sig[736];
    snprintf(sig, sizeof sig, "%s.sig", file);
    if (file_exists(sig)) {
        if (!have_openssl()) { printf("  [2 authenticity]  %s exists but openssl is missing — CANNOT CHECK (that is an answer, not a pass)\n", sig); rc = 1; }
        else {
            char kn[128];
            if (sig_verify_trusted(file, sig, kn))
                printf("  [2 authenticity]  OFFICIAL — signature verifies against trusted key `%s` (keys/official)\n", kn);
            else { printf("  [2 authenticity]  FAIL — a signature exists but NO trusted key verifies it. Either you do not trust the publisher yet (drop their .pub into keys/official) or the file is not what they signed\n"); rc = 1; }
        }
    } else printf("  [2 authenticity]  unsigned — LOCAL (development) grade. Fine for your own tree; an OFFICIAL source would be refused without a signature\n");
    printf("  [1 transport]     TLS is the fetcher's layer (curl) — it says who the SERVER is, never what the BYTES are\n");
    printf("  [4 access]        auth (tokens/mTLS) lives in tool config, outside the source — it grants ACCESS, not integrity\n");
    printf("  note: a hash pin fixes CONTENT, not IDENTITY — the first pin (TOFU) and the decision to trust a key remain HUMAN acts\n");
    return rc;
}

// ── **페처** (RFC-0032 D4 · RFC-0033 D5) — 내려받되, **내용이 신뢰의 전부다.** ──
//
// ★ URL 의존은 **핀 없이는 안 내려받는다**(E-DEP-UNPINNED-URL): 핀 없는 다운로드는
//   "오늘 서버가 준 것"을 믿는 것이고, 그것이 바로 공급망 사고의 문장이다.
//   핀이 있으면 미러가 어디든 상관없다 — 해시가 같으면 같은 패키지다(content-addressing).
// ★ 캐시는 **내용 주소**다: `~/.lowent/cache/<hash>.low`. 같은 바이트는 한 번만 산다.
static bool is_url(const char *s) {
    return strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0 ||
           strncmp(s, "file://", 7) == 0;
}
static bool fetch_url(const char *url, const char *to) {
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "curl -fsSL -o \"%s\" \"%s\" 2>/dev/null", to, url);
    if (system(cmd) == 0) return true;
    snprintf(cmd, sizeof cmd, "wget -qO \"%s\" \"%s\" 2>/dev/null", to, url);
    return system(cmd) == 0;
}
// 핀된 URL 을 캐시에서 얻거나(없으면) 받아 검증해 넣는다. 성공 시 out 에 캐시 경로.
static bool cache_fetch(const char *name, const char *url, const char *pin, char out[600]) {
    const char *home = getenv("HOME");
    if (!home) { fprintf(stderr, "E-PKG-FETCH: no HOME — nowhere to keep the content cache\n"); return false; }
    char base[600], cdir[600];
    snprintf(base, sizeof base, "%s/.lowent", home); mkdir(base, 0755);
    snprintf(cdir, sizeof cdir, "%s/.lowent/cache", home); mkdir(cdir, 0755);
    snprintf(out, 600, "%s/%s.low", cdir, pin);
    if (file_exists(out)) return true;   // 내용 주소 — 이름이 곧 검증된 정체다
    char tmp[700];
    snprintf(tmp, sizeof tmp, "%s.fetch.%d", out, (int)getpid());
    // ★ 미러 먼저 (RFC-0033 D5) — 미러는 **내용 주소 저장소**다: <base>/<hash>.low.
    //   해시는 어차피 아래에서 대조하므로 미러는 **신뢰가 필요 없다**(trustless).
    bool got = false;
    for (int m = 0; m < g_cfg.nmirror && !got; m++) {
        char mu[900];
        snprintf(mu, sizeof mu, "%s/%s.low", g_cfg.mirror[m], pin);
        got = fetch_url(mu, tmp);
    }
    if (!got && !fetch_url(url, tmp)) {
        remove(tmp);
        fprintf(stderr, "E-PKG-FETCH: cannot download `%s` from %s%s (curl/wget failed) — check the "
                        "URL, or vendor the file and point `from` at the local copy\n",
                name, url, g_cfg.nmirror ? " (and no mirror had it)" : "");
        return false;
    }
    proven_byte_t *b = NULL; proven_size_t n = 0;
    if (!read_file(tmp, &b, &n)) { remove(tmp); fprintf(stderr, "E-PKG-FETCH: fetched but cannot read %s\n", tmp); return false; }
    char hex[65];
    hash_hex(b, n, hex);
    free(b);
    if (strcmp(hex, pin) != 0) {
        remove(tmp);
        fprintf(stderr, "E-DEP-HASH: `%s` fetched from %s does NOT match the pin.\n"
                "  pinned  %s\n  actual  %s\n"
                "  A server that returns different bytes under a pinned name is exactly the "
                "supply-chain attack the pin exists to stop. Nothing was installed.\n",
                name, url, pin, hex);
        return false;
    }
    if (rename(tmp, out) != 0) { remove(tmp); fprintf(stderr, "E-PKG-FETCH: cannot place %s into the cache\n", name); return false; }
    return true;
}

// `lowentc fetch` — 매니페스트의 URL 의존을 전부 내려받아 캐시에 넣는다(핀 검증).
//   핀이 없는 것은 **받아서 해시만 보여 준다** — 그 줄을 매니페스트에 적는 것(TOFU 의 승인)은
//   사람 몫이다. 도구가 몰래 핀을 적으면 그것은 확인이 아니라 눈감기다.
static int cmd_fetch(void) {
    low_pkg_t pkg = { 0 };
    if (!low_pkg_find(".", &pkg)) { fprintf(stderr, "lowentc fetch: no pkg.low found\n"); return 2; }
    proven_allocator_t heap = proven_heap_allocator();
    char perr[512];
    if (!low_pkg_load(heap, &pkg, perr)) { fprintf(stderr, "%s\n", perr); return 1; }
    int nurl = 0, bad = 0;
    for (int i = 0; i < pkg.ndeps; i++) {
        const low_pkg_dep_t *d = &pkg.deps[i];
        if (!is_url(d->source)) continue;
        nurl++;
        if (d->hash[0]) {
            char cp[600];
            if (cache_fetch(d->name, d->source, d->hash, cp)) printf("fetched %s → %s\n", d->name, cp);
            else bad++;
        } else {
            char tmp[700];
            const char *home = getenv("HOME");
            snprintf(tmp, sizeof tmp, "%s/.lowent-fetch.%d", home ? home : "/tmp", (int)getpid());
            if (!fetch_url(d->source, tmp)) { remove(tmp); fprintf(stderr, "lowentc fetch: cannot download %s (%s)\n", d->name, d->source); bad++; continue; }
            proven_byte_t *b = NULL; proven_size_t n = 0;
            if (read_file(tmp, &b, &n)) {
                char hex[65];
                hash_hex(b, n, hex);
                free(b);
                printf("UNPINNED %s — the server sent bytes hashing to:\n  %s\n"
                       "  pin it in %s:\n  use %s from \"%s\" hash \"%s\" .\n",
                       d->name, hex, pkg.path, d->name, d->source, hex);
            }
            remove(tmp);
        }
    }
    if (!nurl) printf("lowentc fetch: no URL dependencies in %s\n", pkg.path);
    return bad ? 1 : 0;
}

// `lowentc install <file.low> [--user|--system|--prefix <dir>]` — 라이브러리를 스코프에 넣는다
//   (RFC-0033 D4). 기본 = **--user**(`~/.lowent/lib`, 권한 불요). 이름은 파일 이름이 아니라
//   **파일 안의 `module` 선언**이고, from-상대 의존은 **함께** 복사한다(반쪽 설치는 설치가 아니다).
static bool copy_file(const char *from, const char *to) {
    proven_byte_t *b = NULL; proven_size_t n = 0;
    if (!read_file(from, &b, &n)) return false;
    FILE *o = fopen(to, "wb");
    if (!o) { free(b); return false; }
    size_t w = fwrite(b, 1, n, o);
    fclose(o); free(b);
    return w == n;
}
static int cmd_install(int nargs, char **args) {
    const char *src = NULL, *prefix = NULL;
    bool sys = false;
    for (int i = 0; i < nargs; i++) {
        if (strcmp(args[i], "--user") == 0) { }
        else if (strcmp(args[i], "--system") == 0) sys = true;
        else if (strcmp(args[i], "--prefix") == 0 && i + 1 < nargs) prefix = args[++i];
        else if (!src) src = args[i];
    }
    if (!src) { fprintf(stderr, "usage: lowentc install <file.low> [--user | --system | --prefix <dir>]\n"); return 2; }
    proven_byte_t *b = NULL; proven_size_t n = 0;
    if (!read_file(src, &b, &n)) { fprintf(stderr, "lowentc install: cannot read %s\n", src); return 1; }
    // 모듈 이름 = 파일 안의 선언
    char mod[64] = { 0 };
    for (const char *p = (const char *)b; p && (proven_size_t)(p - (const char *)b) < n; ) {
        while (*p == ' ' || *p == '\n') p++;
        if (strncmp(p, "module ", 7) == 0) {
            p += 7;
            const char *q = p; while (*q && *q != ' ' && *q != '\n' && *q != '.') q++;
            if (q - p > 0 && q - p < 64) snprintf(mod, sizeof mod, "%.*s", (int)(q - p), p);
            break;
        }
        p = strchr(p, '\n');
    }
    if (!mod[0]) { free(b); fprintf(stderr, "lowentc install: %s declares no `module` — there is nothing to name the installation after\n", src); return 1; }
    char dest[600];
    if (prefix) snprintf(dest, sizeof dest, "%s", prefix);
    else if (sys) {
        char self[512] = { 0 };
        ssize_t r = readlink("/proc/self/exe", self, sizeof self - 1);
        if (r <= 0) snprintf(self, sizeof self, ".");
        char *sl = strrchr(self, '/'); if (sl) *sl = 0;
        snprintf(dest, sizeof dest, "%s/../std", self);
    } else {
        const char *home = getenv("HOME");
        if (!home) { free(b); fprintf(stderr, "lowentc install: no HOME — use --prefix <dir>\n"); return 1; }
        char base[600];
        snprintf(base, sizeof base, "%s/.lowent", home); mkdir(base, 0755);
        snprintf(dest, sizeof dest, "%s/.lowent/lib", home);
    }
    mkdir(dest, 0755);
    // 본체 + from-상대 의존을 함께 (기준은 소스 파일의 dir — RFC-0032 "경로는 선언한 파일 기준")
    char sdir[600];
    snprintf(sdir, sizeof sdir, "%s", src);
    { char *sl = strrchr(sdir, '/'); if (sl) *sl = 0; else snprintf(sdir, sizeof sdir, "."); }
    char to[736];
    snprintf(to, sizeof to, "%s/%s.low", dest, mod);
    FILE *o = fopen(to, "wb");
    if (!o || fwrite(b, 1, n, o) != n) { if (o) fclose(o); free(b); fprintf(stderr, "lowentc install: cannot write %s (need --prefix or permissions?)\n", to); return 1; }
    fclose(o);
    char hex[65]; hash_hex(b, n, hex);
    printf("installed %s → %s (%.12s…)\n", mod, to, hex);
    // 전이: 이 파일의 from-상대 의존
    proven_allocator_t heap = proven_heap_allocator();
    static char dn[MAX_DEPS_PER_FILE][64], dpp[MAX_DEPS_PER_FILE][256];
    int dtotal = 0;
    int nd = deps_of(heap, b, n, dn, dpp, MAX_DEPS_PER_FILE, &dtotal);
    if (dtotal > nd) {
        fprintf(stderr, "E-DEP-LIMIT: this file declares %d dependencies and the installer carries "
                "%d — installing only part of them would put an INCOMPLETE package on disk. "
                "Refusing is the honest answer (split the module, or raise MAX_DEPS_PER_FILE "
                "and say why)\n", dtotal, nd);
        free(b);
        return 1;
    }
    free(b);
    for (int d = 0; d < nd; d++) {
        if (!dpp[d][0] || dpp[d][0] == '/' || strstr(dpp[d], "://")) continue;
        char from[736], toc[736];
        snprintf(from, sizeof from, "%s/%s", sdir, dpp[d]);
        snprintf(toc, sizeof toc, "%s/%s", dest, dpp[d]);
        if (strchr(dpp[d], '/')) continue;   // 하위 dir 의존은 아직 — 정직하게 알린다
        if (copy_file(from, toc)) printf("installed dep %s → %s\n", dn[d], toc);
        else fprintf(stderr, "lowentc install: WARNING — could not copy dependency %s (%s); the installed module will not resolve it\n", dn[d], from);
    }
    return 0;
}

// ★ 사용법 — 기능만이 아니라 **읽는 곳**(환경변수·파일 위치·외부 도구)도 전부 말한다.
//   도구가 무엇을 참조하는지 말하지 않으면 사용자는 동작을 재현할 수도 감사할 수도 없다.
static void print_usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s <mode> [options] <file.low>… [args…]\n"
        "\n  서브커맨드 (RFC-0033 — 같은 일의 짧은 이름; 플래그 언어도 그대로 산다)\n"
        "    check · test · doc · fmt · ir · lock   = 아래 같은 이름의 플래그\n"
        "    run [entry] [args…]    엔트리를 VM 으로 돌린다 (생략 = pkg.low entry → src/main.low)\n"
        "    build [entry] [--target T --config F]  네이티브 실행파일 (emit-c → cc, out/<이름>;\n"
        "                           증분 = 방출된 C 의 내용해시 캐시 ~/.lowent/cache/build/)\n"
        "    add <name> <source>    의존을 pkg.low 에 **해시 핀과 함께** 적는다\n"
        "    install <f> [--user|--system|--prefix D]  라이브러리 설치 (기본 ~/.lowent/lib, 전이 의존 포함)\n"
        "    fetch                  매니페스트의 URL 의존을 받아 캐시에 넣는다 (핀 검증; 무핀=해시 제시)\n"
        "    key new N | key list   ed25519 키 (비밀=~/.lowent/keys/private, 신뢰 루트=keys/official)\n"
        "    sign F [--key N]       분리 서명 F.sig 를 만든다 (발행자 = 진본의 답, RFC-0032 D7)\n"
        "    verify F               4층을 분리해 답한다: 무결(해시·핀) · 진본(서명·신뢰 키) · 전송 · 접근\n"
        "\n  무엇을 하나 (하나 고른다 — 기본 모드는 없다)\n"
        "    --check              검사만 한다 (계약·효과·소유·가시성·계층·능력)\n"
        "    --run OP [args…]     VM 으로 그 op 을 돌린다 (슬라이스 인자는 [a,b,c])\n"
        "    --emit-c             C 를 찍는다 — **네이티브 빌드**(volatile·인라인 asm 이 여기서 진짜가 된다)\n"
        "    --test               `test` 블록을 돌린다\n"
        "    --fmt                정규형으로 찍는다 (괄호 = 렌더링 계층, RFC-0046 R5)\n"
        "    --doc [--doc-out D]  문서를 낸다\n"
        "    --ir | --cst | -t | --ops    IR · 나무 · 토큰 · op 모양을 덤프한다\n"
        "\n  무엇으로 짓나\n"
        "    --config FILE        구성 해결본(lowent.config) — `config <opt>` 이 comptime 상수로 읽는다 (RFC-0036)\n"
        "    --lock FILE          의존을 **내용 해시**로 고정한다. 바이트가 바뀌면 거절 (RFC-0032)\n"
        "    --lock-write         지금 무엇을 상대로 빌드하는지 락파일로 찍는다 (매니페스트 옆 pkg.lock 에도)\n"
        "    --pkg FILE           pkg.low 매니페스트 위치를 명시한다 (기본: 첫 파일 dir 에서 위로 찾는다)\n"
        "    --target T           x86_64 | arm64 | cortex_m | riscv64 | mips_be  (machine.* 를 결정, RFC-0054)\n"
        "    --flat | --nest      arity 정규화를 끄는 **대조 스위치** / 그 계측을 보고한다\n"
        "    --emit-ldscript [--fixed-bytes N]  프리스탠딩 고정 창의 링커 스크립트 조각 (RFC-0112 D3)\n"
        "    --fixed-bytes N      VM 의 고정 창 크기 — 다른 보드를 흉내 낸다 (기본: 타깃이 정한다)\n"
        "    --emit-h | --emit-ld | --emit-db | --no-fast | --no-elemsl | --no-carry | --conc-t0 | --no-main | --why-slow | --zones  (C 방출 곁가지)\n"
        "\n  누가 읽나\n"
        "    --version | -V       도구가 자기 버전을 말한다 — 버그 보고가 재현 가능해진다\n"
        "    --diag-json          진단을 **JSON 한 줄씩**(JSONL) 낸다 — 산문을 정규식으로 뜯지\n"
        "                         않아도 되게. 코드가 계약이고 msg 는 편의다 (REQ-0003)\n"
        "\n  도구가 읽는 곳 (이 목록 밖은 읽지 않는다 — ambient 재지정 없음)\n"
        "    HOME                 사용자 루트의 유일한 환경변수. ~/.lowent/{lib,keys/{private,official},cache,config}\n"
        "    <install>            실행파일(/proc/self/exe) 기준 상대: ../std (표준 모듈) · ../config · ../keys/official\n"
        "                         (개발 트리에서는 ../../lib 도 본다)\n"
        "    pkg.low / pkg.lock   첫 파일의 dir 에서 **위로** 걸어 찾는다(≤8단; --pkg 로 명시)\n"
        "    ./lowpm.local        프로젝트 설정 — 사용자 config 를 이긴다 (우선순위: CLI > 프로젝트 > 사용자 > 전역)\n"
        "    설정 키              policy any|local-only|official-only · mirror <base>  (비밀·미러=소스 밖, RFC-0032 D5)\n"
        "    외부 도구 폴백       cc(build) · curl→wget(fetch) · openssl(key/sign/verify) — 없으면 없다고 말한다\n"
        "\n  여러 .low 를 주면 **하나의 컴파일 단위**로 잇는다. `use <n> from \"<path>\" .` 는\n"
        "  스스로 링크된다 — 경로는 **그것을 선언한 파일 기준**이다.\n",
        argv0);
}

int main(int argc, char **argv) {
    atexit(low_hwm_dump);   // ★ 고정 표의 최고수위 — `LOW_HWM=1` 일 때만 찍는다
    // ── 서브커맨드 번역 (RFC-0033 D6) — 플래그 언어는 그대로 살아 있다(스크립트·골든 호환) ──
    static char *xargv[80]; int xargc = 0;
    if (argc >= 2 && argv[1][0] != '-') {
        const char *fl = sub_flag(argv[1]);
        if (fl) argv[1] = (char *)fl;
        else if (strcmp(argv[1], "build") == 0) return cmd_build(argc - 2, argv + 2);
        else if (strcmp(argv[1], "add") == 0) return cmd_add(argc >= 3 ? argv[2] : NULL, argc >= 4 ? argv[3] : NULL);
        else if (strcmp(argv[1], "install") == 0) return cmd_install(argc - 2, argv + 2);
        else if (strcmp(argv[1], "fetch") == 0) { cfg_load(); return cmd_fetch(); }
        else if (strcmp(argv[1], "key") == 0) return cmd_key(argc - 2, argv + 2);
        else if (strcmp(argv[1], "sign") == 0) return cmd_sign(argc - 2, argv + 2);
        else if (strcmp(argv[1], "verify") == 0) return cmd_verify(argc - 2, argv + 2);
        else if (strcmp(argv[1], "run") == 0) {
            // `lowentc run [entry] [args…]` → `--run main <entry> [args…]` (엔트리 생략 = 매니페스트/관례)
            static char entry[600]; char err[256]; low_pkg_t pk = { 0 };
            const char *earg = (argc >= 3 && strstr(argv[2], ".low")) ? argv[2] : NULL;
            if (!resolve_entry(earg, &pk, entry, err)) { fprintf(stderr, "lowentc run: %s\n", err); return 2; }
            xargv[xargc++] = argv[0];
            xargv[xargc++] = (char *)"--run"; xargv[xargc++] = (char *)"main";
            xargv[xargc++] = entry;
            for (int q = earg ? 3 : 2; q < argc && xargc < 79; q++) xargv[xargc++] = argv[q];
            xargv[xargc] = NULL;
            argv = xargv; argc = xargc;
        }
    }
    bool want_tokens = false, want_cst = false, want_fmt = false, want_check = false, want_doc = false,
         want_ir = false, want_emitc = false;
    // ★★ **모듈 링크** — S5 코어의 마지막 벽.
    //   지금까지 파일 **하나**만 받았다. 그래서 `use <mod>` 가 아무것과도 대조되지 않았고
    //   (W-NOT-YET 가 정직하게 그렇게 말했다), `tests` 절이 다른 파일의 op 을 걸 수도 없었다.
    //   이제 여러 .low 를 **하나의 컴파일 단위**로 잇는다: 파일마다 렉스·파스하고
    //   **폼 배열을 합친다.** 이름은 단위 전체에서 보인다.
    #define MAX_FILES 32
    const char *paths[MAX_FILES]; int npaths = 0;
    bool want_lock_write = false, want_why = false, want_emith = false, want_emitld = false;
    bool want_emitdb = false;
    bool want_emitproof = false;
    bool want_emitevents = false;
    const char *doc_out = NULL, *run_op = NULL;
    bool want_test = false, want_ops = false, want_nest = false, want_flat = false;
    bool want_zones = false;
    bool want_erasure = false;   // RFC-0053 §8-9 — def 마다의 지우기 계수
    proven_i64 run_args[64]; proven_size_t nrun_args = 0;
    // ★ 모양 있는 인자 — `[a,b,c]` 는 **슬라이스 하나**다. 하나라도 있으면 모양 있는 매핑을
    //   쓴다(슬라이스가 둘인 op 도 실행할 수 있다). 없으면 옛 평평한 매핑 그대로다.
    // ★★★ **여기 16 은 파라미터 한도의 낡은 사본이었다** (WO-0207). `--run` 이 인자를 담는 칸이
    //   16 이라, 파라미터가 그보다 많은 op 은 **부를 수가 없었다**(`E-VM-ARITY`) — 한도를 32 로
    //   올려 놓고 그것을 시험할 길이 없던 셈이다. 차등 퍼저가 «닿을 수 없는 op» 으로 신고해 드러났다.
    //   ☞ *한도를 올릴 때는 그 한도를 **쓰는** 길도 함께 봐야 한다.*
    low_ir_arg_t shaped[LOW_HDR_MAXP]; proven_size_t nshaped = 0; bool any_bracket = false;
    const char *rawargs[64]; proven_size_t nraw = 0;   // ★ D2′ — 엔트리 실행용 원문 인자
    // ★ 브래킷 인자 상한 — 256 바이트였다. `slice u64` 는 8 바이트가 한 원소라 **32 원소**가
    //   상한이었고, 스크래치를 명령 수에 비례해 받는 op(regex 의 캡처 슬롯 ni×16)은 그 벽에
    //   막혀 **CLI 로 시험할 수가 없었다.** 도구가 못 재는 것은 아무도 안 재게 된다(교훈 4).
    static proven_u8 sbufs[16][8192];
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) want_tokens = true;
        else if (strcmp(argv[i], "--cst") == 0) want_cst = true;
        else if (strcmp(argv[i], "--fmt") == 0) want_fmt = true;
        else if (strcmp(argv[i], "--check") == 0) want_check = true;
        else if (strcmp(argv[i], "--diag-json") == 0) g_diag_json = true;
        else if (strcmp(argv[i], "--doc") == 0) want_doc = true;
        else if (strcmp(argv[i], "--doc-out") == 0 && i + 1 < argc) { want_doc = true; doc_out = argv[++i]; }
        // ★★★ `--config <file>` — **해결본**(RFC-0036 D5: `lowent.config`, 생성물·기계 소유).
        //   평평한 `name value` 줄들이다. 손잡이는 프로그램이 **선언**하고(`build option …`),
        //   값은 **여기서** 고른다. 그리고 `config <name>` 이 그것을 comptime 상수로 읽는다.
        else if (strcmp(argv[i], "--lock") == 0 && i + 1 < argc) lock_load(argv[++i]);
        else if (strcmp(argv[i], "--lock-write") == 0) want_lock_write = true;
        else if (strcmp(argv[i], "--pkg") == 0 && i + 1 < argc) g_pkg_path = argv[++i];
        else if (strcmp(argv[i], "--no-fast") == 0) low_cbe_set_no_fast(true);
        else if (strcmp(argv[i], "--no-elemsl") == 0) low_cbe_set_no_elemsl(true);
        else if (strcmp(argv[i], "--no-carry") == 0) low_cbe_set_no_carry(true);
        else if (strcmp(argv[i], "--conc-t0") == 0) low_cbe_set_conc_t0(true);
        else if (strcmp(argv[i], "--why-slow") == 0) { want_why = true; want_emitc = true; }
        else if (strcmp(argv[i], "--no-main") == 0) low_cbe_set_no_main(true);
        else if (strcmp(argv[i], "--emit-h") == 0) { want_emith = true; want_emitc = true; }
        // ★★★ **`--emit-ld`** — 이 프로그램을 링크하려면 무엇이 필요한가 (2026-07-20).
        //   ☞ 전엔 언어가 그것을 **말할 수 없었고**, 골든이 `-lm` 을 37 곳에서 손으로 붙였다.
        //     손으로 적은 것은 곧 **두 번째 표현**이고 갈린다 — `--emit-h` 를 도구가 내게
        //     만든 것과 **같은 논증**이다(교훈 7).
        //   ⇒ `cc $(lowentc --emit-ld f.low) …` 로 쓴다.
        else if (strcmp(argv[i], "--emit-ld") == 0) { want_emitld = true; want_emitc = true; }
        // ★★★ `--emit-db` — 내용주소 해시를 사이드파일로 (RFC-0012). 증분 빌드의 **전제**다.
        else if (strcmp(argv[i], "--emit-db") == 0) { want_emitdb = true; want_emitc = true; }
        // ★★★ `--emit-proof` — **증명 운반 검사**(RFC-0086). 지운 검사마다 규칙 id 와 그 규칙이
        //   쓴 수를 낸다. 신뢰 대상을 *분석기 수천 줄* 에서 *독립 검증기 수백 줄* 로 줄인다.
        else if (strcmp(argv[i], "--emit-proof") == 0) { want_emitproof = true; }
        // ★★★ `--emit-events` — **모델의 입력을 IR 에서 뽑는다**(16장 ⑥′). 차용 이벤트를 낸다.
        //   유계 전수 모델 체크는 **합성한** 열을 돌린다 ⇒ 진짜 프로그램의 열을 같은 모델에
        //   먹이면 모델↔구현 간극이 **이 방출 함수 하나**로 줄어든다.
        else if (strcmp(argv[i], "--emit-events") == 0) { want_emitevents = true; }
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            FILE *cf = fopen(argv[++i], "r");
            if (!cf) { fprintf(stderr, "cannot open config: %s\n", argv[i]); return 2; }
            char ln[256];
            low_ir_config_reset();
            while (fgets(ln, sizeof ln, cf)) {
                char nm[64], vl[64];
                if (ln[0] == '#' || ln[0] == '\n') continue;
                if (sscanf(ln, "%63s %63s", nm, vl) == 2) low_ir_config_add(nm, vl);
            }
            fclose(cf);
        }
        // ★★★ **도구가 자기 버전을 말한다** (2026-08-01, v1 선언과 함께 신설).
        //   그전까지 `lowentc --version` 은 *"cannot read '--version'"* 이라고 답했다 —
        //   파일 이름인 줄 안 것이다. 버전을 못 말하는 도구는 **버그 보고를 받을 수 없다**:
        //   무엇이 그 답을 냈는지 재현할 수가 없다. 릴리스를 선언하는 순간 이건 결함이다.
        //   ☞ 숫자는 `LOW_VERSION` 한 곳에서 온다 — 문서와 도구가 어긋나면 게이트가 잡는다.
        else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            printf("lowentc %s (Lowent %s) — on %s\n",
                   LOW_VERSION, LOW_VERSION, PROVEN_VERSION_STRING);
            // ★★★ **"버전" 한 낱말이 네 가지를 뜻하고 있었다** (RFC-0089 R4 · 단계 E).
            //   `1.0.0` 은 안에서는 *"RFC-0088 증거 기준 통과"* 인데 밖에서는 **호환성 약속**으로
            //   읽힌다. 네 축을 각자 부르고, **안 얼린 것은 안 얼렸다고 말한다.**
            // ★★★ **epoch 는 이제 열려 있다 — 잠정으로**(2026-08-05, 소유자 선언).
            //   여기서 **가장 조심할 것은 반올림**이다: 사람은 "열렸다" 를 "얼었다" 로 읽는다.
            //   ⇒ 약속하는 것과 **아직 약속하지 않는 것**을 같은 크기로 적는다.
            printf("  release (lowentc)   %s   — RFC-0088 evidence bar passed (NOT \"finished\")\n"
                   "  language revision   %s   — surface/semantics; moves when keywords/builtins change\n"
                   "  compatibility epoch %s (%s)\n"
                   "                              ★ OPEN, PROVISIONALLY. An epoch is the ORIGIN you\n"
                   "                              start measuring compatibility from — hence a date.\n"
                   "                              PROMISED TODAY:  the ABI surface is recorded\n"
                   "                                (docs/abi-surface.h) and gated, so it can no\n"
                   "                                longer change SILENTLY. A change is ANNOUNCED.\n"
                   "                              NOT YET PROMISED:  that it will not change. The\n"
                   "                                language is still in development; the surface MAY\n"
                   "                                still move. `provisional` is exactly that gap\n"
                   "  maturity            %s   — (not stable-core)\n"
                   "  ☞ 수(키워드·빌트인·골든…)는 STATUS.md 에 있다 — 도구가 재고 게이트가 지킨다\n",
                   LOW_RELEASE, LOW_LANG_REV, LOW_ABI_EPOCH, LOW_ABI_EPOCH_STATUS, LOW_MATURITY);
            return 0;
        }
        // ★ `--no-smt` — SMT 백엔드를 끈다. **측정용 대조군**이다(후속 M): 같은 바이너리로
        //   켠 판/끈 판을 재야 그 이득이 실측이 된다. 끄면 느려질 뿐 틀리지 않는다.
        else if (strcmp(argv[i], "--no-smt") == 0) low_ir_set_smt(false);
        // ★ `--emit-smt` — SMT 백엔드가 던진 질의를 **표준 형식(SMT-LIB2)** 으로 낸다.
        //   외부 솔버(Z3·CVC5)가 같은 답을 내는지 대조하는 게이트의 재료다.
        else if (strcmp(argv[i], "--emit-smt") == 0) low_ir_set_smt_emit(true);
        else if (strcmp(argv[i], "--ir") == 0) want_ir = true;
        else if (strcmp(argv[i], "--emit-c") == 0) want_emitc = true;
        else if (strcmp(argv[i], "--run") == 0 && i + 1 < argc) run_op = argv[++i];
        else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) { low_ir_set_target(argv[++i]); }
        // ★★★★ **고정 창의 크기** (RFC-0112 D3(5) · WO-0211) — VM 이 다른 보드를 흉내 낸다.
        //   안 주면 타깃이 정한다(호스티드 64 KiB · 프리스탠딩은 `--emit-ldscript` 가 적는 기본 크기).
        else if (strcmp(argv[i], "--fixed-bytes") == 0 && i + 1 < argc) {
            char *e = NULL; unsigned long long v = strtoull(argv[++i], &e, 10);
            if (!e || *e || v == 0) {
                fprintf(stderr, "E-CLI-FIXED: --fixed-bytes wants a positive byte count, got `%s`\n", argv[i]);
                return 2;
            }
            low_ir_set_fixed_bytes((proven_size_t)v);
        }
        // ★★★★ **링커 스크립트 조각** (RFC-0112 D3(2) · 소유자 결정 2ⓑ). 프리스탠딩 프로그램의 고정 창은
        //   실행 파일에 크기가 박히지 않는다 — 보드의 링커 스크립트가 두 경계 기호를 정하고, 부팅하면 그
        //   사이가 쓰인다. 이 조각은 그 기본값이다: 보드 스크립트의 `SECTIONS` 안(RAM 영역의 `.bss` 뒤)에
        //   `INCLUDE` 하고, 크기는 보드에 맞게 고친다. 프로그램 없이 낸다.
        else if (strcmp(argv[i], "--emit-ldscript") == 0) {
            unsigned long long fb = 0;
            for (int j = 1; j < argc; j++)
                if (strcmp(argv[j], "--fixed-bytes") == 0 && j + 1 < argc) fb = strtoull(argv[j + 1], NULL, 10);
            if (!fb) fb = LOW_FIXED_BYTES_BOARD;
            printf("/* lowentc --emit-ldscript — the FIXED WINDOW of a freestanding Lowent program (RFC-0112 D3).\n"
                   " * INCLUDE this inside SECTIONS of your board's linker script, in a RAM region after .bss.\n"
                   " * The program carves `cap allocator` / `effects alloc` / region blocks from between the two\n"
                   " * symbols; it never learns the size at build time, so change it here for your board. */\n"
                   ".lw_fixed (NOLOAD) : ALIGN(8)\n"
                   "{\n"
                   "    __lw_fixed_start = .;\n"
                   "    . = . + %llu;\n"
                   "    __lw_fixed_end = .;\n"
                   "}\n", fb);
            return 0;
        }
        else if (strcmp(argv[i], "--test") == 0) want_test = true;
        else if (strcmp(argv[i], "--ops") == 0) want_ops = true;
        else if (strcmp(argv[i], "--zones") == 0) want_zones = true;
        // ★ **def 마다의 지우기 계수** (RFC-0053 §8-9). `--ir` 의 사람 읽는 요약은
        //   파일 합계인데, `--ir` 은 딸려온 모듈까지 낮추므로 그 합계는 **수입-가중**이다
        //   (공유 라이브러리가 소비자 수만큼 다시 세어진다). 세는 쪽이 def 해시로 중복을
        //   없앨 수 있게, 한 줄에 하나씩 기계가 읽는 모양으로 낸다.
        else if (strcmp(argv[i], "--erasure") == 0) { want_erasure = true; want_ir = true; }
        // ★ arity 단일화의 **대조 스위치**. 나무를 세운 뒤 IR 을 내면 def 해시가
        //   **한 비트도 달라선 안 된다**(RFC-0012 내용주소화). 골든이 매번 그것을 잰다.
        else if (strcmp(argv[i], "--nest") == 0) want_nest = true;
        // ★ 나무를 **끈다** — 대조용. 세 오라클(해시·진단·런타임)이 이 둘을 매번 맞대 본다.
        else if (strcmp(argv[i], "--flat") == 0) want_flat = true;
        // ★ D2′ — 엔트리 실행(`--run main`)의 나머지 CLI 는 **프로그램 인자 원문**이다.
        //   다른 op 의 --run 인자 규칙(숫자·[슬라이스])은 그대로 둔다 — main 만 소비한다.
        else if (run_op && npaths && strcmp(run_op, "main") == 0 && nraw < 64) {
            rawargs[nraw++] = argv[i];
        }
        else if (run_op && npaths && argv[i][0] == '[' && nshaped < LOW_HDR_MAXP) {
            any_bracket = true;
            proven_size_t k = nshaped, n = 0;
            bool over = false;
            for (const char *p = argv[i] + 1; *p && *p != ']'; ) {
                if (*p == ',' || *p == ' ') { p++; continue; }
                char *e = NULL; long long v = strtoll(p, &e, 10);
                if (e == p) break;
                // ★★★ **조용히 자르지 않는다.** 전에는 `n < 8192` 를 루프 조건에 두어
                //   8192 바이트를 넘는 배열이 **말 없이 잘렸다** — 12000 을 주면 1024 개
                //   u64 가 들어오고, 프로그램은 *"버퍼가 짧다"* 며 0 을 답했다.
                //   그 0 을 보고 사람은 **자기 코드를 뒤진다.** 자름은 도구가 말해야 한다.
                if (n >= sizeof sbufs[0]) { over = true; break; }
                sbufs[k][n++] = (proven_u8)v; p = e;
            }
            if (over) {
                fprintf(stderr,
                        "E-CLI-ARGTOOBIG: this `[...]` argument has more than %zu elements — "
                        "refusing rather than truncating (a silently short slice makes the "
                        "program answer \"buffer too small\" and sends you hunting in your own "
                        "code). Split the run, or raise the limit in impl/src/main.c.\n",
                        sizeof sbufs[0]);
                return 2;
            }

            shaped[nshaped++] = (low_ir_arg_t){ .is_slice = true, .bytes = sbufs[k], .n = n };
        }
        // ★ f64 인자. 예전엔 `.` 이 있어야만 부동소수로 봤다 — 그래서 **`1e10` 은
        //   파일 이름으로 읽혔고**(VM), 네이티브는 그것을 **조용히 1 로** 읽었다
        //   (strtoll 이 앞 글자만 먹는다). **같은 인자, 두 해석.** 차등 퍼저가 찾았다.
        //   ⇒ 규칙: **strtod 가 토큰을 끝까지 먹으면** 부동소수다(`.` 이든 지수든).
        else if (run_op && npaths && nshaped < LOW_HDR_MAXP && argv[i][0] &&
                 (strchr(argv[i], '.') || strchr(argv[i], 'e') || strchr(argv[i], 'E')) &&
                 ({ char *e_ = NULL; (void)strtod(argv[i], &e_); e_ && *e_ == '\0'; })) {
            double dv = strtod(argv[i], NULL);
            proven_i64 bits; memcpy(&bits, &dv, 8);
            shaped[nshaped++] = (low_ir_arg_t){ .is_flt = true, .v = bits };
            any_bracket = true;   // 모양 있는 매핑으로 간다(평평한 매핑은 정수만 안다)
        }
        else if (run_op && npaths && nrun_args < 64 &&
                 strspn(argv[i], "-0123456789") == strlen(argv[i]) && argv[i][0]) {
            run_args[nrun_args++] = strtoll(argv[i], NULL, 10);
            if (nshaped < LOW_HDR_MAXP)
                shaped[nshaped++] = (low_ir_arg_t){ .is_slice = false,
                                                    .v = strtoll(argv[i], NULL, 10) };
        }
        else if (npaths < MAX_FILES) paths[npaths++] = argv[i];
        // ★★★ **넘치는 인자를 조용히 버리지 않는다** (2026-09-09 · REQ-0012).
        //   전에는 서른세 번째 파일이 그냥 사라졌고, 그 파일의 틀린 몸통은 검사되지 않은 채
        //   `--check` 가 초록으로 끝났다. 요청을 반만 들어주고 «됐다» 고 말하면 안 된다.
        else {
            fprintf(stderr, "E-DEP-LIMIT: more than %d input files were given; the rest would be "
                    "IGNORED and their code would go unchecked. Refusing is the honest answer "
                    "(compile in parts, or raise MAX_FILES and say why)\n", MAX_FILES);
            return 1;
        }
    }
    const char *path = npaths ? paths[0] : NULL;
    if (!path) {
        fprintf(stderr, "lowentc %s — the Lowent compiler (VM + C back end) — on %s\n",
                LOW_VERSION, PROVEN_VERSION_STRING);
        // ★ 사용법이 **가진 기능의 절반을 안 적고 있었다**(`--emit-c`·`--config`·`--lock`·`--ops`).
        //   도구가 자기가 할 수 있는 일을 말하지 않으면, 그 기능은 **없는 것과 같다** —
        //   그리고 아무도 그것을 재지 않는다(교훈 4: 도구가 표현 못 하는 것은 아무도 실행하지 않는다).
        //   같은 논증이 **읽는 곳**에도 선다: 도구가 어떤 환경변수·파일 위치를 참조하는지
        //   말하지 않으면, 사용자는 도구의 동작을 재현할 수도 감사할 수도 없다.
        print_usage(argv[0]);
        return 2;
    }

    proven_allocator_t heap = proven_heap_allocator();
    proven_byte_t *bufs[MAX_FILES] = { 0 };
    proven_size_t lens[MAX_FILES] = { 0 };
    proven_size_t total = 0;
    for (int fi = 0; fi < npaths; fi++) {
        if (!read_file(paths[fi], &bufs[fi], &lens[fi])) {
            fprintf(stderr, "lowentc: cannot read '%s'\n", paths[fi]);
            return 1;
        }
        total += lens[fi];
    }
    // ── **pkg.low 매니페스트** (RFC-0032/0033 · 2A) — 첫 파일의 dir 에서 위로 걸어 찾는다. ──
    //   찾으면: ① 옆의 pkg.lock 을 자동 로드(--lock 이 이기고) ② from-생략 2계층(선언 의존)의
    //   근거가 되고 ③ --lock-write 가 pkg.lock **파일**을 쓴다.
    low_pkg_t pkg = { 0 };
    cfg_load();   // 2D: CLI > 프로젝트(lowpm.local) > 사용자(~/.lowent/config) > 전역(<install>/config)
    {
        char sdir[512];
        if (g_pkg_path) {
            snprintf(pkg.path, sizeof pkg.path, "%s", g_pkg_path);
            snprintf(pkg.dir, sizeof pkg.dir, "%s", g_pkg_path);
            char *sl = strrchr(pkg.dir, '/');
            if (sl) *sl = 0; else snprintf(pkg.dir, sizeof pkg.dir, ".");
            pkg.found = true;
        } else {
            snprintf(sdir, sizeof sdir, "%s", path);
            char *sl = strrchr(sdir, '/');
            if (sl) *sl = 0; else snprintf(sdir, sizeof sdir, ".");
            (void)low_pkg_find(sdir, &pkg);
        }
        if (pkg.found) {
            char perr[512];
            if (!low_pkg_load(heap, &pkg, perr)) { fprintf(stderr, "%s\n", perr); return 1; }
            if (!g_have_lock && !want_lock_write) {
                char lp[600]; snprintf(lp, sizeof lp, "%s/pkg.lock", pkg.dir);
                FILE *lf = fopen(lp, "r");
                if (lf) { fclose(lf); lock_load(lp); }
            }
            // ★ --lock-write: 매니페스트가 **의존을 선언할 때만** pkg.lock 파일을 쓴다 —
            //   락은 의존 선언의 그림자다. 정체만 적은 매니페스트(이 저장소의 루트 pkg.low 같은)
            //   옆에 락을 만들면, 발견 걷기가 그것을 **모든 하위 컴파일에** 자동 적용해 버린다.
            if (want_lock_write && pkg.ndeps > 0) {
                char lp[600]; snprintf(lp, sizeof lp, "%s/pkg.lock", pkg.dir);
                g_lock_out = fopen(lp, "w");
                if (g_lock_out)
                    fprintf(g_lock_out, "# pkg.lock — content hashes (BLAKE3) of every dependency.\n"
                                        "# Regenerate with: lowentc --lock-write <entry.low>\n");
            }
        }
    }

    // ★★★ **`use … from "…"` 를 실제로 로드한다** (RFC-0032 D3) — 그리고 **해시로 고정한다**(D4).
    //   지금까지 `use` 는 **같은 CLI 에 준 파일들** 안에서만 풀렸다. 즉 의존을 **소스가 말하지
    //   못했다** — 빌드 명령이 말했다. 이제 **프로그램이 자기 의존을 말하고**, 도구가 그것을
    //   따라가서 **내용을 확인한다.**
    {
        static char dnames[MAX_DEPS_PER_FILE][64], dpaths[MAX_DEPS_PER_FILE][256];
        static bool std_origin[MAX_FILES] = { false };   // 이 파일이 예약(std)에서 왔는가 — 전이 의존까지 락 제외
        int scanned = 0;
        while (scanned < npaths) {
            int fi = scanned++;
            int dtotal = 0;
            int nd = deps_of(heap, bufs[fi], lens[fi], dnames, dpaths, MAX_DEPS_PER_FILE, &dtotal);
            // ★★★ **자르지 않는다 — 말한다** (REQ-0012). 전에는 열일곱 번째 `use` 부터 **로드가
            //   안 됐고**, 그 파일의 틀린 몸통은 검사되지 않은 채 `--check` 가 초록으로 끝났다.
            if (dtotal > nd) {
                fprintf(stderr, "E-DEP-LIMIT: `%s` declares %d dependencies and this tool loads %d "
                        "per file. Loading only part of them would type-check only part of the "
                        "program and still report success — so it refuses instead. Split the file, "
                        "or raise MAX_DEPS_PER_FILE and say why\n", paths[fi], dtotal, nd);
                return 1;
            }
            for (int d = 0; d < nd; d++) {
                // ★ 단위 전체의 파일 수도 같은 규율이다: 넘치면 **거절한다**(조용히 빼지 않는다).
                if (npaths >= MAX_FILES) {
                    fprintf(stderr, "E-DEP-LIMIT: this compilation unit reaches %d files, which is "
                            "all the tool carries — the rest would be left out and the missing code "
                            "would go unchecked. Refusing is the honest answer\n", MAX_FILES);
                    return 1;
                }
                // ★★★ **의존 경로는 그것을 선언한 파일 기준이다.**
                //   CWD 기준이면 **어디서 컴파일하느냐에 따라 프로그램의 뜻이 달라진다** —
                //   같은 소스가 두 가지를 뜻하게 된다. (그리고 실제로 그랬다: 저장소 루트에서
                //   부르면 의존을 못 찾았다. 정규식-렉싱 게이트가 그것을 잡았다.)
                static char resolved[16][512];
                const char *base = paths[fi];
                const char *slash = strrchr(base, '/');
                const low_pkg_dep_t *mdep = NULL;   // 매니페스트 선언 의존으로 해소됐는가 (해시 핀)
                bool via_std = std_origin[fi];      // 예약(std) 해소 — 신뢰 뿌리는 설치다, 프로젝트 락이 아니다
                if (dpaths[d][0] == 0) {
                    // ── from-생략 3계층 해소 (RFC-0011 §6.3): 예약 std → 매니페스트 → 형제 dir ──
                    std_scan(argv[0]);
                    const char *sp = std_find(dnames[d]);
                    char sib[600];
                    if (slash) snprintf(sib, sizeof sib, "%.*s%s.low", (int)(slash - base + 1), base, dnames[d]);
                    else        snprintf(sib, sizeof sib, "%s.low", dnames[d]);
                    FILE *sf = fopen(sib, "rb"); bool sib_ok = sf != NULL; if (sf) fclose(sf);
                    mdep = pkg.found ? low_pkg_dep_find(&pkg, dnames[d]) : NULL;
                    if (sp) {
                        // ★★★ **자기 자신은 가림이 아니다** (2026-07-26).
                        //   std 모듈이 **다른 std 모듈**을 부르면(예: `growvec` 이 `gvec` 을),
                        //   그 "형제 파일" 은 곧 **std 파일 자신**이다. 그것을 가림으로 읽어
                        //   E-NAME-RESERVED 를 내면 **표준 라이브러리끼리 서로 못 부른다** —
                        //   *네 프로그램이 틀렸다* 고 말하지만 틀린 것은 도구다(교훈 5).
                        //   ⇒ 실경로가 같으면 조용히 통과시킨다. 다르면 규칙은 그대로 문다.
                        if (sib_ok) {
                            char r1[PATH_MAX], r2[PATH_MAX];
                            if (realpath(sib, r1) && realpath(sp, r2) && strcmp(r1, r2) == 0)
                                sib_ok = false;
                        }
                        if (mdep || sib_ok) {
                            fprintf(stderr, "E-NAME-RESERVED: `use %s .` — this is a RESERVED (standard) "
                                    "module name, and %s also provides it. A local file must not shadow a "
                                    "standard module: rename it, or import it explicitly with "
                                    "`use %s from \"<path>\" .` so the choice is written down (RFC-0011 §6.3)\n",
                                    dnames[d], mdep ? "the manifest" : "a sibling file", dnames[d]);
                            return 1;
                        }
                        snprintf(resolved[d], sizeof resolved[d], "%s", sp);
                        via_std = true;
                    } else if (mdep) {
                        if (is_url(mdep->source)) {
                            // ★ 정책(2D · RFC-0033 D8): local-only 는 네트워크 자체를 거절한다.
                            if (strcmp(g_cfg.policy, "local-only") == 0) {
                                fprintf(stderr, "E-POLICY-LOCAL: dependency `%s` points at a URL (%s) but "
                                        "the policy is local-only (lowpm.local / ~/.lowent/config) — vendor "
                                        "the file, or change the policy on purpose\n", dnames[d], mdep->source);
                                return 1;
                            }
                            // ★ URL 은 **핀 없이는 안 받는다** — 핀 없는 다운로드는 "오늘 서버가
                            //   준 것"을 믿는 것이다. 핀이 있으면 content-addressed 캐시가 산다.
                            if (!mdep->hash[0]) {
                                fprintf(stderr, "E-DEP-UNPINNED-URL: dependency `%s` points at a URL (%s) "
                                        "with NO hash pin — that would mean trusting whatever the server "
                                        "sends today. Run `lowentc fetch` to see the content hash, then "
                                        "pin it: `use %s from \"%s\" hash \"<hex>\" .` (RFC-0032 D4)\n",
                                        dnames[d], mdep->source, dnames[d], mdep->source);
                                return 1;
                            }
                            char cp[600];
                            if (!cache_fetch(dnames[d], mdep->source, mdep->hash, cp)) return 1;
                            // ★ official-only (2C ②층): 해시(무결)로는 부족하다 — **발행자의 서명**이
                            //   신뢰 키로 검증돼야 한다. 서명은 <url>.sig 의 분리 서명이다.
                            if (strcmp(g_cfg.policy, "official-only") == 0) {
                                char su[900], st[736], kn[128];
                                snprintf(su, sizeof su, "%s.sig", mdep->source);
                                snprintf(st, sizeof st, "%s.sig", cp);
                                if (!file_exists(st) && !fetch_url(su, st)) remove(st);
                                if (!file_exists(st) || !sig_verify_trusted(cp, st, kn)) {
                                    fprintf(stderr, "E-POLICY-UNSIGNED: policy is official-only, and `%s` "
                                            "has no signature a TRUSTED key verifies (%s.sig). The hash pin "
                                            "proves the bytes are unchanged; it cannot prove WHO published "
                                            "them — that is the signature's question (RFC-0032 D7)\n",
                                            dnames[d], mdep->source);
                                    return 1;
                                }
                            }
                            snprintf(resolved[d], sizeof resolved[d], "%s", cp);
                        }
                        else if (mdep->source[0] == '/') snprintf(resolved[d], sizeof resolved[d], "%s", mdep->source);
                        else snprintf(resolved[d], sizeof resolved[d], "%s/%s", pkg.dir, mdep->source);
                    } else if (sib_ok) {
                        snprintf(resolved[d], sizeof resolved[d], "%s", sib);
                    } else {
                        continue;   // 미해소 — 단위 안에 있거나(W 불요) 밖이면 W-USE-EXTERNAL 이 말한다
                    }
                } else if (dpaths[d][0] != '/' && slash) {
                    int bl = (int)(slash - base + 1);
                    snprintf(resolved[d], sizeof resolved[d], "%.*s%s", bl, base, dpaths[d]);
                } else {
                    snprintf(resolved[d], sizeof resolved[d], "%s", dpaths[d]);
                }
                char *dp = resolved[d];
                bool dup = false;
                for (int q = 0; q < npaths; q++) if (strcmp(paths[q], dp) == 0) dup = true;
                if (dup) continue;
                proven_byte_t *db = NULL; proven_size_t dl = 0;
                if (!read_file(dp, &db, &dl)) {
                    fprintf(stderr, "E-DEP-MISSING: `use %s from \"%s\"` — cannot read that source. "
                            "A dependency the tool cannot see is a dependency nobody checked\n",
                            dnames[d], dp);
                    return 1;
                }
                // ★ 매니페스트의 `hash "<hex>"` 핀 — lock 과 같은 계약, 선언 자리만 다르다.
                if (mdep && mdep->hash[0]) {
                    char hex[65];
                    hash_hex(db, dl, hex);
                    if (strcmp(hex, mdep->hash) != 0) {
                        fprintf(stderr, "E-DEP-HASH: dependency `%s` (%s) does NOT match the manifest pin.\n"
                                "  pinned  %s\n  actual  %s\n"
                                "  The manifest promised specific BYTES; these are different bytes.\n",
                                dnames[d], dp, mdep->hash, hex);
                        return 1;
                    }
                }
                // ★ 예약(std) 모듈은 프로젝트 락의 대상이 아니다 — 신뢰 뿌리는 **설치 자체**다
                //   (2D: impl dir 고정/서명. 프로젝트 락에 std 를 적으면 도구 업그레이드마다
                //    모든 프로젝트의 락이 "변조" 로 붉어진다 — 락은 프로젝트의 의존만 세라).
                if (!via_std && !dep_verify(dnames[d], dp, db, dl, want_lock_write)) return 1;
                // ★★★ 경로는 **적재 파일별 안정 버퍼**에 산다. `resolved[d]` 를 그대로 가리키게
                //   했더니 — 다음 파일 스캔의 d=0 이 그 문자열을 **덮어썼고**, dup 검사가
                //   (같은 포인터끼리 비교돼) 전이 의존을 **조용히 건너뛰었다.** vec→alloc 같은
                //   2단 사슬이 파일 하나로는 안 드러나던 잠복 결함이다.
                // ★★★ **다이아몬드: 같은 모듈 이름이 다른 바이트로 두 번 들어온다** (2026-07-26).
                //
                //   `ext_a` 가 `base` v1 을, `ext_b` 가 `base` v2 를 부르면 한 단위에 `module base`
                //   가 둘 있게 된다. 전에는 그 결과가 **조용히 틀린 프로그램**이었다:
                //   `--check` 는 `E-NAME-DUP`(=*"네가 이름을 두 번 선언했다"*)이라 **엉뚱한 것을
                //   탓했고**, `--run` 은 **먼저 들어온 쪽으로 둘 다 묶어** 답을 냈다 —
                //   ext_b 는 자기가 v1 을 쓰고 있다는 것을 알 길이 없었다.
                //
                //   ★ 버전 통일(Cargo 식 그래프 해소)은 아직 없다. 그래서 **고르지 않는다** —
                //     고를 수 없을 때 하나를 조용히 고르는 것이 바로 그 조용한 오답이다.
                //     ⇒ **거절하고, 둘 다 이름을 말한다.** 없는 기능을 있는 척하지 않는다.
                //   ☞ 같은 **바이트**면 충돌이 아니다(같은 것을 두 경로로 벤더링한 흔한 경우) —
                //     그건 진짜 공유이고 조용히 합친다.
                bool same_bytes_already = false;   // ★ 같은 모듈·같은 바이트가 **이미 단위에 있다**
                {
                    char mn[64] = { 0 };
                    if (mod_name_of(db, dl, mn, sizeof mn) && mn[0]) {
                        for (int q = 0; q < npaths; q++) {
                            char pn[64] = { 0 };
                            if (!mod_name_of(bufs[q], lens[q], pn, sizeof pn)) continue;
                            if (strcmp(pn, mn) != 0) continue;
                            // ★★★★★ **같은 바이트 = 공유. 그러면 정말로 합쳐야 한다** (2026-08-07).
                            //   이 줄은 여태 **오류만 건너뛰고 파일은 그대로 또 실었다** — 주석은
                            //   *"조용히 합친다"* 라고 적어 두었는데 동작은 합치지 않았다.
                            //   ⇒ 같은 파일이 **두 철자**로 오면(내가 적은 경로 + 표준 위치에서
                            //     찾은 `…/impl/build/../../lib/alloc.low`) 아래 경로 문자열 dup
                            //     검사가 못 잡고, 모듈의 **모든 선언이 두 벌**이 됐다.
                            //   실측: `lib/alloc.low` 을 쓰는 모듈끼리 짝지으면 `E-NAME-DUP:
                            //     \`allocs\` (module) is declared twice` — 이름 충돌처럼 보이지만
                            //     **같은 파일을 두 번 읽은 것**이었다. 아픈 쌍 7 이 이 하나였다.
                            //   ☞ **약속을 적어 두고 지키지 않으면, 그 주석이 다음 사람을 속인다.**
                            if (lens[q] == dl && memcmp(bufs[q], db, dl) == 0) { same_bytes_already = true; break; }
                            char h1[65], h2[65];
                            hash_hex(bufs[q], lens[q], h1);
                            hash_hex(db, dl, h2);
                            fprintf(stderr,
                                "E-PKG-DIAMOND: module `%s` arrives TWICE with DIFFERENT bytes — the build "
                                "would have to pick one, and picking silently is how the wrong program gets "
                                "built (it used to: the second copy was dropped and its callers were bound to "
                                "the first without being told).\n"
                                "  already loaded  %s\n    blake3 %s\n"
                                "  now required    %s\n    blake3 %s\n"
                                "  There is no version unification yet (no semver graph resolution), so the "
                                "tool does NOT choose. Make the two agree — vendor one copy, or pin both "
                                "requesters to the same source in the manifest.\n",
                                mn, paths[q], h1, dp, h2);
                            return 1;
                        }
                    }
                }
                if (same_bytes_already) continue;   // ★ 이미 단위에 있다 — 두 번 싣지 않는다
                static char loaded[MAX_FILES][512];
                snprintf(loaded[npaths], sizeof loaded[npaths], "%s", dp);
                std_origin[npaths] = via_std;
                paths[npaths] = loaded[npaths];
                bufs[npaths] = db;
                lens[npaths] = dl;
                total += dl;
                npaths++;
            }
        }
        if (want_lock_write) { if (g_lock_out) fclose(g_lock_out); return 0; }   // 락을 찍는 것이 이번 실행의 전부다
    }

    proven_byte_t *buf = bufs[0];
    proven_size_t len = lens[0];
    proven_u8str_view_t src = { .ptr = buf, .size = len };   // --doc / --fmt 는 첫 파일 기준

    // node arena (CST) — must outlive evaluation
    proven_size_t node_bytes = total * 64 + (proven_size_t)65536;
    void *node_mem = malloc(node_bytes);
    proven_arena_t node_arena = proven_arena_create((proven_mem_mut_t){ .ptr = node_mem, .size = node_bytes });
    proven_allocator_t nodes0 = proven_arena_as_allocator(&node_arena);

    // ★ 파일마다 렉스·파스하고 **폼을 합친다** — 하나의 컴파일 단위.
    low_lex_result_t lex = { 0 };
    low_parse_result_t pr = { 0 };
    proven_size_t nforms0 = 0;   // 첫 파일의 폼 수
    {
        low_cst_t **all = (low_cst_t **)malloc(sizeof(low_cst_t *) * 4096);
        proven_size_t nall = 0;
        bool ok = true;
        proven_result_array_t da = PROVEN_ARRAY_INIT(heap, low_diag_t, 8);
        proven_array_t diags = da.value;
        for (int fi = 0; fi < npaths; fi++) {
            proven_u8str_view_t s2 = { .ptr = bufs[fi], .size = lens[fi] };
            low_lex_result_t lx = low_lex(heap, s2);
            // ★ 토큰 덤프도 **파일의 것**이다 — 의존까지 찍으면 그것은 단위의 덤프다.
            //   (`--fmt` 와 같은 규칙: 파일 도구는 파일을 찍는다.)
            if (want_tokens && fi == 0) dump_tokens(&lx);
            dump_diags("lex diagnostics", &lx.diags);
            // ★★★ **렉스 오류도 오류다** (2026-08-01). 여기서 `lx.ok` 를 안 보아서, 렉서만
            //   잡는 진단(E-LIT-RANGE 가 첫 사례)이 붙은 파일이 진단을 **찍으면서**
            //   `check: ok` 를 인쇄했다 — 종료코드는 1 인데 화면은 초록이라 말했다.
            //   게이트가 `grep "check: ok"` 로 읽으므로 그 한 줄은 **거짓말**이 된다.
            if (!lx.ok) ok = false;
            low_parse_result_t p2 = low_parse(nodes0, heap, &lx.tokens);
            dump_diags("parse diagnostics", &p2.diags);
            if (!p2.ok) ok = false;
            // ★★★ **이 파일에서 온 노드에 파일을 새긴다** (단계 V). 파일마다 줄이 1 부터 다시
            //   시작하므로, 줄만 실은 진단은 다중 파일 단위에서 **어느 파일인지 말하지 못한다.**
            for (proven_size_t q = 0; q < p2.nforms; q++) cst_stamp_file(p2.forms[q], paths[fi]);
            for (proven_size_t q = 0; q < p2.nforms && nall < 4096; q++) all[nall++] = p2.forms[q];
            if (fi == 0) nforms0 = nall;   // ★ 첫 파일이 어디서 끝나는가 — 서식이 그것만 찍는다
            if (fi == 0) lex = lx;                 // --fmt 는 첫 파일의 토큰을 쓴다
            else { proven_array_destroy(&lx.diags); proven_array_destroy(&lx.tokens); }
            if (p2.forms && fi != 0) heap.free_fn(heap.ctx, p2.forms);
            proven_array_destroy(&p2.diags);
        }
        pr.forms = all; pr.nforms = nall; pr.diags = diags; pr.ok = ok;
    }

    // ★★★ **나무가 기본이다** (2026-07-14). 파서는 여전히 arity 를 모른다(구조=문자열,
    //   RFC-0046) — 나무는 **파스 다음에** 선다. `--flat` 은 그것을 끄는 **대조 스위치**이고,
    //   골든이 매 실행마다 둘을 맞대 본다: **해시가 같고 · 진단이 같고 · 답이 같다.**
    if (!want_flat) {
        proven_size_t nested = 0, gave_up = 0;
        low_nest_set_report_limit(nforms0);   // ★ 보고는 이 파일의 것이다
        low_nest(&pr, nodes0, heap, &nested, &gave_up);
        // ★ 계측기는 **명시적으로 물었을 때만** 말한다. 처음엔 늘 stderr 에 찍었고,
        //   그 한 줄이 골든의 출력 비교 **114개를 깨뜨렸다** — 도구가 멀쩡한데
        //   **계측기가 측정을 망쳤다.** 하마터면 "소비자가 나무를 못 견딘다" 고 오진할 뻔했다.
        // ★★★ **단형화** — 나무가 선 **다음**에 온다(호출 자리를 나무에서 찾는다).
        //   RFC-0021: 제네릭 = comptime 타입 파라미터. 호출마다 구체 op 을 만든다.
        // ★ **서식 경로에서는 단형화하지 않는다** — `--fmt` 은 **원본**을 찍어야 한다.
        //   인스턴스 이름(`twice#rect`)의 `#` 은 렉서가 못 읽는다: 찍으면 **다시 못 읽는다.**
        //   (def-해시 대조가 즉시 잡았다 — 서식기가 **뜻을 바꿨다**.)
        if (!want_fmt) {
            proven_size_t ninst = 0, nsub = 0;
            // ★★★★ RFC-0112 D8 — 객체마다 고른 얼로케이터를 먼저 채운다. 단형화는 **다 채워진** 호출을 본다.
            proven_size_t nd_before = pr.diags.len;
            low_using(&pr, nodes0, heap);
            low_mono(&pr, nodes0, heap, &ninst, &nsub);
            // ★ 파스 뒤에 붙은 진단(using · 단형화)도 **찍는다** — 전엔 `E-MONO-FIXPOINT` 가 여기서 쌓이고
            //   화면에는 안 나왔다(종료 코드만 1). 조용한 거절은 거절이 아니다.
            if (pr.diags.len > nd_before) {
                proven_array_t tail = pr.diags;
                tail.data = (char *)pr.diags.data + nd_before * sizeof(low_diag_t);
                tail.len = pr.diags.len - nd_before;
                dump_diags("unit diagnostics", &tail);
            }
            if (want_nest && ninst)
                fprintf(stderr, "mono: %zu instance(s) from %zu call site(s) — the back end never "
                                "sees a generic (RFC-0021)\n", (size_t)ninst, (size_t)nsub);
        }
        if (want_nest) {
            char why[512]; low_nest_report(why, sizeof why);
            fprintf(stderr, "nest: %zu form(s) bracketed, %zu region(s) left flat"
                            "%s%s\n  (a wrong tree is worse than no tree — this list shrinking IS the progress)\n",
                    (size_t)nested, (size_t)gave_up, why[0] ? " — blame: " : "", why);
        }
    }

    int rc = 0;
    if (want_doc) {
        if (doc_out) {
            int n = low_doc_write(&pr, src, heap, doc_out);
            if (n < 0) fprintf(stderr, "lowdoc: write failed under '%s'\n", doc_out);
            else fprintf(stderr, "lowdoc: wrote %d op(s) → %s/<module>.md + %s/llms.txt\n", n, doc_out, doc_out);
        } else {
            (void)low_doc(&pr, src, heap);
        }
    } else if (want_fmt) {
        // ★★★ **서식은 파일을 찍는다 — 컴파일 단위가 아니다.**
        //   의존을 진짜로 링크하기 시작하자(RFC-0032) 서식기가 **의존 모듈까지 같이 찍었다.**
        //   그러면 그 출력에는 `use …` 와 **그 모듈의 본문이 둘 다** 있고, 다시 읽으면
        //   의존이 **두 번** 들어온다 — 그리고 그것을 **서식 오라클이 즉시 고발했다**
        //   ("서식이 뜻을 바꿨다"). 파일에 있던 것만 찍는다.
        low_parse_result_t p0 = pr;
        p0.nforms = nforms0;
        low_cst_fmt(&p0);
    } else if (want_zones) {
        // ★★★ **안전지대 경계** (RFC-0065 §5) — 최상위 선언마다 이름·시작 줄·끝 줄.
        //   `--ops` 가 op 의 **모양**을 말하듯, `--zones` 는 각 선언의 **경계**를 말한다.
        //   에디터·AI 가 파일을 **파싱하지 않고** 의미 단위로 자를 수 있다.
        for (proven_size_t i = 0; i < pr.nforms; i++) {
            const low_cst_t *f = pr.forms[i];
            if (f->kind != LOW_CST_FORM || f->nkids < 2 || f->kids[0]->kind != LOW_CST_ATOM) continue;
            low_kw_t kw = f->kids[0]->tok.kw;
            const char *what = kw == LOW_KW_MODULE ? "module" : kw == LOW_KW_STRUCT ? "struct"
                             : kw == LOW_KW_ENUM ? "enum" : kw == LOW_KW_FN ? "fn"
                             : kw == LOW_KW_PROC ? "proc" : kw == LOW_KW_ACTOR ? "actor"
                             : kw == LOW_KW_TRAIT ? "trait" : NULL;
            if (!what) continue;
            proven_u32 start = f->kids[0]->tok.line;
            // ★ 끝 = **다음 최상위 선언의 시작 직전 줄**. 그것이 안전지대의 정의다 —
            //   열-0 규칙이 보장하는 바로 그 경계다(RFC-0065). form 내부 토큰 줄에 안 기댄다
            //   (정규화가 그 줄 정보를 지울 수 있다 — 경계는 **표면의 성질**이지 나무의 것이 아니다).
            proven_u32 end = start;
            for (proven_size_t j = i + 1; j < pr.nforms; j++) {
                const low_cst_t *g = pr.forms[j];
                if (g->kind != LOW_CST_FORM || !g->nkids || g->kids[0]->kind != LOW_CST_ATOM) continue;
                low_kw_t gk = g->kids[0]->tok.kw;
                if (gk == LOW_KW_MODULE || gk == LOW_KW_STRUCT || gk == LOW_KW_ENUM ||
                    gk == LOW_KW_FN || gk == LOW_KW_PROC || gk == LOW_KW_ACTOR ||
                    gk == LOW_KW_TRAIT) {
                    end = g->kids[0]->tok.line > start ? g->kids[0]->tok.line - 1 : start;
                    break;
                }
            }
            const low_cst_t *nm = (f->kids[1]->kind == LOW_CST_ATOM) ? f->kids[1] : NULL;
            printf("%s %.*s %u %u\n", what,
                   nm ? (int)nm->tok.lex.size : 1, nm ? (const char *)nm->tok.lex.ptr : "?",
                   start, end);
        }
    } else if (want_cst) {
        low_cst_dump(&pr);
    } else if (want_emitproof) {
        // ★★★ 증명 운반 검사 — 분석을 돌린 뒤 **왜 지웠는지**를 낸다(RFC-0086).
        //   `--ir` 처럼 사람용 산문을 섞지 않는다: 이 출력은 **검증기가 읽는 것**이다.
        low_ir_t ir = low_ir_build(heap, &pr);
        int n = low_ir_emit_proof(&ir, stdout);
        rc = (n >= 0 && ir.ok) ? 0 : 1;
    } else if (want_ir || want_emitc || run_op || want_test || want_ops) {
        // ★ `--ir` 은 **사람이 읽는 덤프**다 — 저자를 향한 경고를 여기서도 켠다.
        //   `--run`/`--emit-c`/`--test` 는 켜지 않는다: 그 출력은 답이거나 C 소스이고,
        //   VM 과 네이티브를 **바이트로** 견주는 검사들이 그것을 읽는다.
        low_ir_set_author_warnings(want_ir);
        low_ir_t ir = low_ir_build(heap, &pr);
        low_ir_set_author_warnings(false);
        if (want_ir && low_ir_build_dropped())
            printf("-- build mode: %zu contract check(s) DROPPED by the mode (RFC-0008 §6.5). "
                   "A mode that removes checks SILENTLY is not an optimisation — it is the "
                   "quiet death of a guarantee. --\n", (size_t)low_ir_build_dropped());
        if (want_ir) {
            low_ir_dump(&ir);
            // ★ RFC-0008 §6.4/Q6 — 계약에서 뽑은 경계값 테스트. **계약이 오라클도 준다.**
            low_ir_ctest_t ct = low_ir_contract_tests(&ir, heap, false);
            printf("\n-- contract-derived boundary tests (RFC-0008 §6.4 · Q6) --\n");
            printf("   %zu op(s) with a contract to derive from → %zu case(s)\n", ct.ops, ct.cases);
            printf("     value axis  : %zu admitted · %zu rejected  (range / requires bounds)\n",
                   ct.admitted, ct.rejected);
            printf("     shape  axis : %zu case(s)  (slices/structs varied ONE AT A TIME — this is "
                   "where the WRITE ops live: encode(src, mut dst). They were outside the oracle)\n",
                   (size_t)ct.shape_cases);
            printf("     length axis : %zu case(s)  (0/1/2/3/4/8 — the EMPTY slice is a boundary too — plus every constant the contract names in `len …`)\n",
                   ct.len_cases);
            // ★ 표가 차서 못 만든 길이 케이스도 **말한다**(RFC-0077 P1-5): 자르고 침묵하면
            //   덜 검사한 것이 안 보인다. 0 이면 아무 말도 안 한다(소음이 아니라 신호여야 한다).
            if (ct.len_dropped)
                printf("     ★ 길이 케이스 %zu 개를 표가 차서 **못 만들었다** — 그만큼 덜 검사했다\n",
                       ct.len_dropped);
            if (ct.budget_skips)
                printf("     ★ %zu case(s) SKIPPED by the budget — the op's COST is an INPUT: either a "
                       "trip count (a boundary value of 2^64-1 would never finish) or a WAIT "
                       "(`time_sleep`, which the step budget cannot see at all — one step, sixty "
                       "seconds). Skipped, not silently passed: the oracle says what it did not "
                       "check\n", ct.budget_skips);
            // ★★★ **여유를 숫자로 말한다** (2026-08-14). 예산은 *"정직한 케이스가 끝나는 자리"*
            //   여야 한다. 그 여유가 얼마나 남았는지 안 보이면, 다음 픽스처 하나가 벼랑을 넘어도
            //   **조용히 건너뛴 채** 초록이 된다. 그래서 완주한 케이스의 최대 걸음을 함께 찍는다.
            if (ct.cases)
                printf("     걸음 여유    : 완주한 케이스의 최대 %llu 걸음 / 예산 %llu "
                       "(정직한 케이스가 예산에 닿으면 그때부터 조용히 건너뛰어진다)\n",
                       (unsigned long long)ct.steps_max_ok, (unsigned long long)ct.step_budget);
            printf("   %zu oracle failure(s)", ct.failures);
            if (!ct.failures)
                printf("  — every admitted input is accepted, every rejected input traps");
            printf("\n");
            // ★ **못 잰 것을 말한다.** 이 수가 0 이 아니면 그 자리에서 오라클은
            //   *아무 말도 하지 않은* 것이다 — 초록도 빨강도 아니다.
            if (ct.unmodelled)
                printf("\n   ? %zu case(s) the oracle could NOT model — a struct argument is filled "
                       "with one repeated byte, so a field-path `requires` cannot be satisfied. "
                       "These are NOT failures of the program: they are places the oracle said nothing",
                       ct.unmodelled);
            if (ct.admitted_traps)
                printf("   ! %zu admitted input(s) trapped in the body — the contract admits "
                       "inputs the op cannot handle\n", ct.admitted_traps);
            if (ct.mode_dropped)
                printf("   ★ (%zu op(s) NOT verifiable: the BUILD MODE dropped their contract "
                       "checks (RFC-0008 §6.5). That is not the program's fault — the mode said so. "
                       "The oracle verifies the CONTRACT, not the BUILD: verify in `debug`)\n",
                       (size_t)ct.mode_dropped);
            if (ct.unbuildable)
                printf("   ★ (%zu op(s) NOT derived: the value axis can only build SCALARS, and these "
                       "take a struct/slice. The oracle used to put 0/1 there and then BLAME THE "
                       "CONTRACT when the body trapped — a MISDIAGNOSIS. It now says it cannot, "
                       "instead of accusing the program)\n", (size_t)ct.unbuildable);
            if (ct.skipped)
                printf("   (%zu op(s) skipped: no contract tighter than the declared types — "
                       "nothing to derive)\n", ct.skipped);
            // ★★★ **match 비용 가시** (RFC-0081 B1) — Rust 는 match 의 디스패치 비용을 저자에게
            //   보여주지 않는다. 여기서는 말한다: 몇 자리인지, 최악 몇 번 비교하는지. 현재 하강은
            //   선형 사슬이라 최악 = arm 수(작은 match 는 Rust 와 동일; 큰 조밀 match 의 결정트리 =
            //   MM9-B2 후속). "재지 않은 비용은 숨은 비용"이라는 규율(RFC-0054)을 match 에도 편다.
            if (ir.match_sites)
                printf("-- match: %zu site(s), %zu arm(s) total, worst-case %zu sequential tag "
                       "compare(s) in a linear match; %zu match(es) lowered to an O(1) JUMP TABLE "
                       "(IRW_SWITCH) — dense & large enough (RFC-0081 MM9-B2). Rust builds decision "
                       "trees but never shows this; here the cost is visible. --\n",
                       ir.match_sites, ir.match_arms, ir.match_worst, ir.match_jt);
        }
        if (want_emitc) {
            if (want_emitdb) {
                int n = low_ir_emit_db(&ir, stdout);
                rc = (n >= 0 && ir.ok) ? 0 : 1;
            } else if (want_emitld) {
                // ★ 중복 없이 한 번씩. 순서는 선언 순서다(재현 가능해야 한다).
                proven_u8str_view_t seen[64]; proven_size_t nseen = 0;
                for (proven_size_t q = 0; q < ir.ndefs; q++) {
                    proven_u8str_view_t L = ir.defs[q].link_lib;
                    if (!ir.defs[q].is_extern || !L.size) continue;
                    bool dup = false;
                    for (proven_size_t z = 0; z < nseen; z++)
                        if (proven_u8str_view_eq(seen[z], L)) dup = true;
                    if (dup || nseen >= 64) continue;
                    seen[nseen++] = L;
                    printf("%s-l%.*s", nseen > 1 ? " " : "", (int)L.size, (const char *)L.ptr);
                }
                if (nseen) printf("\n");
                rc = ir.ok ? 0 : 1;
            } else if (want_emith) {
                int n = low_cbe_emit_header(&ir, stdout);
                // ★★★ **같은 설명 기록으로**(RFC-0089 R5) — `export` 인데 심볼이 안 나오는 자리는
                //   헤더가 주석으로도 말하지만, 도구가 읽을 출구는 **하나**여야 한다.
                //   ☞ 판정은 단계 C 의 기록(`low_ir_plan_t`)에서 **읽기만** 한다.
                if (g_diag_json) {
                    low_cbe_plan(&ir);
                    for (proven_size_t q = 0; q < ir.ndefs; q++) {
                        const low_ir_plan_t *pl = low_cbe_plan_of(q);
                        if (!ir.defs[q].is_export || ir.defs[q].is_extern || !pl->why) continue;
                        char nm[128]; proven_size_t z = ir.defs[q].name.size;
                        if (z >= sizeof nm) z = sizeof nm - 1;
                        memcpy(nm, ir.defs[q].name.ptr, z); nm[z] = '\0';
                        expl_json("W-EXPORT-NOSYM", "warning", "emit-h", NULL, 0, 0,
                                  "`export` means C can call this op: the object defines that symbol",
                                  pl->why, "absent", nm, NULL);
                    }
                }
                rc = (n >= 0 && ir.ok) ? 0 : 1;
            } else if (want_why) {
                // ★★★ **못 하는 일을 말한다.** 어떤 op 이 왜 느린 경로에 남았는지 묻지 못하면,
                //   그 목록은 아무도 줄이지 못한다 — 그리고 아무도 재지 않는다.
                FILE *nul = fopen("/dev/null", "w");
                low_cbe_why(true);
                int n = low_cbe_emit(&ir, nul ? nul : stdout);
                if (nul) fclose(nul);
                proven_size_t slow = 0;
                for (proven_size_t q = 0; q < ir.ndefs; q++) {
                    const char *w = low_cbe_why_of(q);
                    if (!w || !w[0]) continue;
                    slow++;
                    // ★★★ **설명은 한 모양으로 나온다**(RFC-0089 R5): 하강 이유도 진단과 같은
                    //   기록이다 — 여기서는 도구가 **약속과 사실을 둘 다** 안다.
                    if (g_diag_json) {
                        char nm[128]; proven_size_t n = ir.defs[q].name.size;
                        if (n >= sizeof nm) n = sizeof nm - 1;
                        memcpy(nm, ir.defs[q].name.ptr, n); nm[n] = '\0';
                        expl_json("W-CBE-SLOW", "note", "lower", NULL, 0, 0,
                                  "this op lowers to the fast path (natural C: no tag, no box, no 12KB frame)",
                                  w, "unmet", nm, NULL);
                    } else
                    printf("  %-24.*s  %s\n", (int)ir.defs[q].name.size,
                           (const char *)ir.defs[q].name.ptr, w);
                }
                printf("why-slow: %zu / %zu op(s) still on the tagged path\n",
                       (size_t)slow, (size_t)ir.ndefs);
                rc = (n >= 0 && ir.ok) ? 0 : 1;
            } else {
            int n = low_cbe_emit(&ir, stdout);
            rc = (n > 0 && ir.ok) ? 0 : 1;
            }
        }
        // ★★ `--ops` — op 마다 **파라미터의 모양**을 말한다: i=정수 · f=f64 · s=슬라이스 · t=구조체.
        //   자동 차등 퍼저가 인자를 맞추려면 **모양을 물어볼 수 있어야 한다.**
        //   물어볼 방법이 없어서 세 op 에 닿지 못했다 — PRINCIPLES.md §0 교훈 4 그대로:
        //   **도구가 표현 못 하는 모양은 아무도 실행하지 않는다.** 그러면 검증도 못 한다.
        if (want_ops) {
            for (proven_size_t i = 0; i < ir.ndefs; i++) {
                const low_ir_def_t *d = &ir.defs[i];
                if (!d->lowered || d->is_test) continue;
                printf("%.*s", (int)d->name.size, (const char *)d->name.ptr);
                for (proven_size_t p = 0; p < d->nparams; p++)
                    printf(" %c", ((d->param_struct >> p) & 1u) ? 't'
                                : ((d->param_slice  >> p) & 1u) ? 's'
                                : ((d->param_flt    >> p) & 1u) ? 'f' : 'i');
                printf("\n");
            }
            rc = ir.ok ? 0 : 1;
        }

        // ★ def 마다의 지우기 계수 — 세는 쪽이 def 해시로 중복을 없앤다(RFC-0053 §8-9).
        //   `--ir` 요약은 파일 합계이고, 그 합계는 딸려온 모듈까지 세므로 **수입-가중**이다.
        if (want_erasure) {
            printf("\n-- erasure by def (RFC-0053 §8-9) --\n");
            for (proven_size_t i = 0; i < ir.ndefs; i++) {
                const low_ir_def_t *d = &ir.defs[i];
                if (!d->lowered) continue;
                char dh[17];
                for (int b = 0; b < 8; b++)
                    snprintf(dh + b * 2, 3, "%02x", (unsigned)d->def_hash[b]);
                printf("erasure %s %.*s %zu %zu\n", dh,
                       (int)d->name.size, (const char *)d->name.ptr,
                       (size_t)d->checks_proven, (size_t)d->checks_total);
            }
        }

        // ★★★ R2 — `parallel` op 을 **진짜로 쪼개어 돌리고 순차와 비교한다**(DET-1).
        if (want_ir) {
            low_ir_par_t pr2 = low_ir_par_check(&ir, heap, true);
            if (pr2.ops)
                printf("\n-- parallel split (RFC-0009 DET-1 · LowentPar.v: parallel == sequential) --\n"
                       "   %zu op(s) · %zu split schedule(s) run and compared BIT-FOR-BIT with the "
                       "sequential run\n   %zu divergence(s)  — a divergence means the `parallel` "
                       "declaration is a LIE (overlapping writes), or the reduction is NOT "
                       "associative (DET-3)\n",
                       (size_t)pr2.ops, (size_t)pr2.splits, (size_t)pr2.failures);
            if (pr2.failures) rc = 1;
        }

        // ★★ 테스트 러너 — `test N do … end` 이 **드디어 실행된다.**
        if (want_test) {
            low_ir_test_result_t tr = low_ir_run_tests(&ir, heap, true);
            printf("== tests: %zu run, %zu passed, %zu FAILED ==\n",
                   (size_t)tr.total, (size_t)tr.passed, (size_t)tr.failed);
            if (tr.total == 0)
                printf("   (no `test` blocks in this unit — nothing was run, and this says so)\n");
            rc = tr.failed ? 1 : 0;
        }
        if (run_op && !lex.diags.len && !pr.diags.len) {
            // ★ RFC-0030 D2′ — 엔트리(main)가 cap 파라미터를 선언했으면 CLI 의 나머지는
            //   **프로그램 인자**다: cap 자리는 불투명 토큰으로 채우고, 원문 문자열이
            //   `count`/`arg` 가 읽는 목록이 된다. 다른 op 의 --run 규칙은 불변.
            const low_ir_def_t *entry_d = NULL;
            if (strcmp(run_op, "main") == 0)
                for (proven_size_t q = 0; q < ir.ndefs; q++)
                    if (ir.defs[q].name.size == 4 &&
                        memcmp(ir.defs[q].name.ptr, "main", 4) == 0) { entry_d = &ir.defs[q]; break; }
            bool entry_caps = entry_d && entry_d->param_cap;
            proven_u8str_view_t pargs[64];
            low_ir_arg_t captoks[LOW_HDR_MAXP];
            if (entry_caps) {
                for (proven_size_t q = 0; q < nraw && q < 64; q++)
                    pargs[q] = proven_u8str_view_from_cstr(rawargs[q]);
                memset(captoks, 0, sizeof captoks);
            }
            low_ir_run_result_t rr =
                entry_caps
                  ? low_ir_run_argv(&ir, proven_u8str_view_from_cstr(run_op),
                                    captoks, entry_d->nparams, heap, &ir.diags, pargs, nraw)
                  : any_bracket
                  ? low_ir_run_argv(&ir, proven_u8str_view_from_cstr(run_op),
                                    shaped, nshaped, heap, &ir.diags, NULL, 0)
                  : low_ir_run(&ir, proven_u8str_view_from_cstr(run_op),
                               run_args, nrun_args, heap, &ir.diags);
            if (rr.ok) {
                printf("%s(", run_op);
                if (entry_caps) {
                    for (proven_size_t q = 0; q < nraw; q++) printf("%s%s", q ? ", " : "", rawargs[q]);
                } else if (any_bracket) {
                    for (proven_size_t i = 0; i < nshaped; i++) {
                        if (i) printf(", ");
                        if (shaped[i].is_flt) {   // 비트가 아니라 **값**을 찍는다(`.0` 을 지킨다)
                            double dv; memcpy(&dv, &shaped[i].v, 8);
                            char fb[40]; int fn = snprintf(fb, sizeof fb, "%g", dv);
                            bool dot = false;
                            for (int q = 0; q < fn; q++)
                                if (fb[q] == '.' || fb[q] == 'e' || fb[q] == 'n' || fb[q] == 'i') dot = true;
                            printf("%s%s", fb, dot ? "" : ".0"); continue;
                        }
                        if (!shaped[i].is_slice) { printf("%lld", (long long)shaped[i].v); continue; }
                        printf("[");
                        for (proven_size_t j = 0; j < shaped[i].n; j++)
                            printf("%s%u", j ? "," : "", (unsigned)shaped[i].bytes[j]);
                        printf("]");
                    }
                } else {
                    for (proven_size_t i = 0; i < nrun_args; i++)
                        printf("%s%lld", i ? ", " : "", (long long)run_args[i]);
                }
                printf(") = %s\n", rr.text);
                // ★ mut 슬라이스로 **쓴 값**을 되보여 준다 — 안 보이면 쓰기 op 는 검증할 수가 없다.
                //   (C 백엔드도 똑같이 찍는다: 골든의 차등 검사가 두 텍스트를 그대로 비교한다.)
                for (proven_size_t i = 0; i < nshaped && any_bracket; i++) {
                    if (!shaped[i].is_slice) continue;
                    printf("  arg%zu (written) = [", (size_t)i);
                    for (proven_size_t j = 0; j < shaped[i].n; j++)
                        printf("%s%u", j ? "," : "", (unsigned)shaped[i].bytes[j]);
                    printf("]\n");
                }
            }
            rc = rr.ok ? 0 : 1;
        } else if (want_ir) {
            rc = ir.ok ? 0 : 1;
        }
        dump_diags("ir diagnostics", &ir.diags);
        low_ir_free(heap, &ir);
    } else if (want_emitevents) {
        // ★ 차용 이벤트만 낸다 — 진단은 stderr 로 가고, **표준출력은 사실만** 나른다.
        low_region_set_emit_events(true);
        low_region_result_t rr = low_region(heap, &pr);
        rc = rr.ok ? 0 : 1;
    } else if (want_check) {
        low_check_result_t cr = low_check(heap, &pr);
        low_typecheck_result_t tr = low_typecheck(heap, &pr);
        low_contract_result_t kr = low_contract(heap, &pr);
        low_region_result_t rr = low_region(heap, &pr);
        dump_diags("effect diagnostics", &cr.diags);
        dump_diags("type diagnostics", &tr.diags);
        dump_diags("contract diagnostics", &kr.diags);
        dump_diags("region diagnostics", &rr.diags);
        // ★★★ **파싱조차 못 한 프로그램에 초록불을 주고 있었다.**
        //   `if c . return 1 .` 은 E-STMT-NODO(파스 **오류**)를 인쇄한 다음
        //   `== check: ok ==` 와 **종료코드 0** 을 냈다. 도구가 자기가 못 읽은 것을
        //   "검사했다" 고 말한 것이다 — 이 언어가 금지하는 거짓말의 가장 순수한 형태다.
        //   렉스/파스 진단은 **모든 뒤 단계의 전제**다. 그것이 깨지면 뒤의 초록불은 무의미하다.
        bool ok = cr.ok && tr.ok && kr.ok && rr.ok && pr.ok;

        // ★ 정직성: --check 는 **타입·effect·계약·영역**을 본다. 그것이 초록불이어도
        //   op 이 **낮아지지 않으면 실행되지 않는다**(S5 코어 밖). 지금까지 --check 는
        //   그것을 말하지 않았다 — 도구가 검사한 것보다 **많이 주장하고 있었다.**
        //   이 언어가 금지하는 바로 그것이다. 이제 말한다.
        if (ok) {
            low_ir_set_author_warnings(true);          // ★ 저자를 향한 경고는 `--check` 에서 켠다
            low_ir_t ir2 = low_ir_build(heap, &pr);
            low_ir_set_author_warnings(false);
            proven_size_t nlow = 0;
            for (proven_size_t i = 0; i < ir2.ndefs; i++) if (!ir2.defs[i].lowered) nlow++;
            // ★★ IR 이 **단위 자체를 거절했다면**(E-IR-LIMIT — op 이 너무 많다) --check 는
            //   초록불일 수 없다. 그런데 그랬다: 앞의 256 개가 멀쩡히 낮아지고 나머지는 그냥
            //   **사라지므로** nlow == 0 이 되고, 거절 진단이 **한 번도 인쇄되지 않았다.**
            //   도구가 내부적으로 거절해 놓고 "ok" 라고 말했다 — 검사한 것보다 **많이 주장했다.**
            //   ★ 단 **단위 거절(E-IR-LIMIT)만** 이다. E-IR-UNSUP 은 "도구가 아직 못 한다" 이지
            //     "네 프로그램이 틀렸다" 가 아니다 — 그것까지 오류로 만들면 **오진**이 된다(교훈 5).
            // ★★ 규칙을 **정직하게 일반화한다**: `E-IR-UNSUP` 만이 "도구가 아직 못 한다" 이고,
            //   나머지 IR 오류는 전부 **프로그램이 틀렸다** 이다 ⇒ --check 는 초록불일 수 없다.
            //   ★ 전엔 `E-IR-LIMIT` 하나만 거절 신호로 봤다. 그래서 `E-FIELD-MARK`(구조체 필드의
            //     모르는 표기) 같은 새 오류가 나면 IR 은 거절하는데 `--check` 는 **ok 라고 말했다.**
            //     예외 목록을 손으로 유지하는 검사는 **반드시 뒤처진다.**
            bool refused = false;
            for (proven_size_t i = 0; i < ir2.diags.len; i++) {
                const low_diag_t *d2 = PROVEN_ARRAY_GET(&ir2.diags, low_diag_t, i);
                if (d2->sev == LOW_SEV_ERROR && strcmp(d2->code, "E-IR-UNSUP") != 0) refused = true;
            }
            if (refused) {
                dump_diags("lowering diagnostics", &ir2.diags);
                printf("   ^ the tool REFUSED this compilation unit. `--check` cannot be green on a "
                       "unit the compiler will not take.\n");
                ok = false;
            } else if (nlow) {
                // ★ 두 가지를 **가른다** — 이 구분이 중요하다:
                //   E-IR-UNDEF  = 이름이 없다 → **프로그램이 틀렸다.** 오류다.
                //   그 밖(UNSUP) = 구현이 아직 못 한다 → **주의**다. 프로그램은 옳을 수 있다.
                bool undef = false;
                for (proven_size_t i = 0; i < ir2.diags.len; i++)
                    if (strcmp(PROVEN_ARRAY_GET(&ir2.diags, low_diag_t, i)->code, "E-IR-UNDEF") == 0)
                        undef = true;
                dump_diags("lowering diagnostics", &ir2.diags);
                if (undef) {
                    printf("   ^ an undefined name is not a missing feature — the program is wrong.\n");
                    ok = false;
                } else {
                    printf("   note: %zu op(s) pass every static check but CANNOT be lowered "
                           "(outside the S5 core) — they will not run. `--ir` names them.\n", nlow);
                }
            } else {
                // ★★★ **낮춤의 «주의» 도 `--check` 가 보여 준다** (2026-09-07).
                //   여기까지 오면 거절도 없고 못 낮춘 op 도 없다 — 그런데 그때 **경고는
                //   한 줄도 안 보였다.** `W-CONTRACT-IGNORED`(진입 계약이 못 세우는 절)가
                //   `--ir` 에서만 보이면, 그 경고가 향하는 사람은 **못 본다**: 저자는 보통
                //   `--check` 를 부른다.
                //   ☞ *경고는 그것을 읽어야 할 사람의 눈앞에 나와야 한다. 안 보이는 경고는 없는 것이다.*
                proven_size_t nwarn = 0;
                for (proven_size_t i = 0; i < ir2.diags.len; i++)
                    if (PROVEN_ARRAY_GET(&ir2.diags, low_diag_t, i)->sev == LOW_SEV_WARNING) nwarn++;
                if (nwarn) dump_diags("lowering diagnostics", &ir2.diags);
            }
            low_ir_free(heap, &ir2);
        }
        printf("== check: %s ==\n", ok ? "ok" : "violations");
        // ★ JSON 모드에서는 **판정도 한 줄**로 낸다. 진단만 기계가 읽고 결론은 산문에서
        //   긁어야 한다면 절반만 구조화한 것이다 — 그 절반이 도구를 깨뜨린다.
        if (g_diag_json)
            fprintf(stderr, "{\"result\":\"%s\",\"exit\":%d}\n", ok ? "ok" : "violations", ok ? 0 : 1);
        rc = ok ? 0 : 1;
        proven_array_destroy(&cr.diags);
        proven_array_destroy(&tr.diags);
        proven_array_destroy(&kr.diags);
        proven_array_destroy(&rr.diags);
    } else {
        // ★★ 기본 모드는 **옛 트리워킹 인터프리터**(low_eval.c, 1422 줄)를 돌렸다.
        //   그리고 그것이 돌리는 것은 **명세에 없는 두 번째 언어**였다:
        //     let a 6 ; let b 7        ← `be` 가 없다
        //     return fn x to expr …    ← fn 람다
        //     print · record · list · map · `;`
        //   SPEC-002 어휘 목록에 **하나도 없다.** 명세는 한 언어를 말하고, 도구의 **기본 모드**는
        //   다른 언어를 실행하고 있었다 — 한 저장소 안의 **검사되지 않는 두 언어**.
        //   이 프로젝트가 금지하는 것의 가장 큰 사례다(PRINCIPLES.md §0).
        //   ★ 그리고 그것이 `fn`·`as`·`fail`·`give`·`unit` 이 살아 있던 **유일한 이유**였다.
        //   지웠다. 이제 무엇을 할지 **말해야** 한다 — 도구가 마음대로 고르지 않는다.
        fprintf(stderr,
                "%s: say what you want done — there is no default mode any more.\n"
                "  the old tree-walking interpreter (and the second, undocumented language it ran)\n"
                "  has been deleted: THE LANGUAGE IS ONE. use --check / --run OP / --ir / --test / --doc.\n\n",
                argv[0]);
        print_usage(argv[0]);   // ★ 무엇을 잘못했는지 말했으면, 무엇이 가능한지도 말한다
        rc = 2;
    }
    if (lex.diags.len || pr.diags.len) rc = 1;

    if (pr.forms) heap.free_fn(heap.ctx, pr.forms);
    proven_array_destroy(&pr.diags);
    free(node_mem);
    proven_array_destroy(&lex.tokens);
    proven_array_destroy(&lex.diags);
    free(buf);
    return rc;
}
