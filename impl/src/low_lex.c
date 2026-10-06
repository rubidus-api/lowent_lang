// low_lex.c — L0 lexer for the lowent point-closure surface.
//
// Design (SPEC-MINI §2, SPEC-002 부록 G, RFC-0046 rev.d):
//   * Newlines are non-significant (R7) but line/col are tracked for diagnostics.
//   * A '.' is a *closer* (DOT) unless it is glued: inside a qualified name
//     (net.http) or a float fraction (1.5). Glued dots are consumed by the word /
//     number scanners, so any '.' that reaches the top level is isolated.
//   * ',' is the argument closer (§0 D8); '(' ')' are decoration groups (R5).
//   * rem …EOL and note TERM … TERM are comments (consumed, not emitted).
//   * text [proc] TERM \n … \n TERM is a raw text literal (TERM closes only in column 1, alone).
//
// Lexeme views borrow the source buffer (zero-copy); tokens live in an arena array.
#include "proven/utf.h"
#include "low_lex.h"

typedef struct {
    proven_u8str_view_t src;
    proven_size_t       pos;
    proven_u32          line;
    proven_u32          col;
    low_lex_result_t   *out;
    bool                in_expr;  // inside an `expr …` island: `-`/`+` are operators, not signs
    // ★★★ **여는 것들의 스택** — `(` 과 블록 여는 낱말을 **함께** 쌓는다.
    //   왜 함께인가: 괄호가 `do…end` 를 담을 수 있고(`return (make p do … end) .`),
    //   블록이 괄호를 담을 수 있다. 둘이 **엇갈리면** 안 된다.
    //
    //   ★ 그리고 이것이 없어서 **괄호 하나를 안 닫으면 파일 전체가 죽었다**:
    //     렉서의 `paren` 이 파일 전역 카운터라, 안 닫힌 `(` 뒤로는 **모든 개행이 안 닫혔고**,
    //     그 뒤의 op 들이 통째로 그 괄호 안으로 빨려 들어갔다. **괄호가 자기 짝 밖으로 탈출했다.**
    //   이제 **`end` 를 넘어갈 수 없다**: `end` 위에 열린 괄호가 남아 있으면 그것은 오류이고,
    //   거기서 **닫아 버린다**(피해를 그 블록 안에 가둔다).
    proven_u8         open[128];  // 0 = '(' · 1 = 블록
    proven_size_t     nopen;
    bool              nest_told;  // ★ 넘침을 **한 번만** 말한다(low_push_open)
    int                 paren;    // 스택 위의 '(' 개수 — 괄호 안의 개행은 **잇는다**
    low_tok_kind_t      last_kind;// last emitted token (for newline soft-close decision)
    low_kw_t            last_kw;
} low_lexer_t;

enum { LOW_OPEN_PAREN = 0, LOW_OPEN_BLOCK = 1 };

// ── byte classes ──────────────────────────────────────────────────────────────
static inline bool low_is_space(proven_byte_t c)  { return c == ' ' || c == '\t' || c == '\r'; }
static inline bool low_is_nl(proven_byte_t c)     { return c == '\n'; }
static inline bool low_is_digit(proven_byte_t c)  { return c >= '0' && c <= '9'; }
static inline bool low_is_alpha(proven_byte_t c)  {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
static inline bool low_is_ident(proven_byte_t c)  { return low_is_alpha(c) || low_is_digit(c); }
static inline bool low_is_hex(proven_byte_t c)    {
    return low_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// ── cursor ────────────────────────────────────────────────────────────────────
static inline bool low_at_end(const low_lexer_t *l) { return l->pos >= l->src.size; }
static inline proven_byte_t low_peek(const low_lexer_t *l) {
    return low_at_end(l) ? (proven_byte_t)0 : l->src.ptr[l->pos];
}
static inline proven_byte_t low_peek_at(const low_lexer_t *l, proven_size_t k) {
    proven_size_t i = l->pos + k;
    return i >= l->src.size ? (proven_byte_t)0 : l->src.ptr[i];
}
static proven_byte_t low_adv(low_lexer_t *l) {
    proven_byte_t c = l->src.ptr[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; } else { l->col++; }
    return c;
}
static inline proven_u8str_view_t low_span(const low_lexer_t *l, proven_size_t start) {
    return (proven_u8str_view_t){ .ptr = l->src.ptr + start, .size = l->pos - start };
}
static const proven_u8str_view_t LOW_EMPTY_VIEW = { .ptr = (const proven_byte_t *)"", .size = 0 };

// ── output ────────────────────────────────────────────────────────────────────
static void low_emit(low_lexer_t *l, low_tok_kind_t kind, low_kw_t kw,
                     proven_u8str_view_t lex, proven_u8str_view_t aux,
                     proven_u32 line, proven_u32 col) {
    low_token_t t = { .kind = kind, .kw = kw, .lex = lex, .aux = aux, .line = line, .col = col };
    if (proven_array_push(&l->out->tokens, &t) != PROVEN_OK) {
        l->out->ok = false;  // out of memory: caller sees ok == false
    }
    l->last_kind = kind; l->last_kw = kw;
}

static void low_diag(low_lexer_t *l, const char *code, const char *msg,
                     proven_u32 line, proven_u32 col) {
    low_diag_t d = { .sev = LOW_SEV_ERROR, .code = code, .msg = msg, .line = line, .col = col };
    (void)proven_array_push(&l->out->diags, &d);
    l->out->ok = false;
}

#define LOW_NEST_MAX 128

// ★★★★★ **넘치면 말한다** (2026-09-01, 소유자 결정 ①).
//
//   여는 괄호와 블록을 미는 자리 **둘**이 `if (l.nopen < 128) …` 이었다 — 129 번째를
//   **조용히 버렸다.** 그러면 그 짝을 닫을 때 스택이 비어 있고, 진단이 *"짝이 없는 `)`"*
//   (`E-PAREN-STRAY`) 라고 말한다. **괄호는 짝이 맞는데.**
//   실측(2026-09-01): 깊이 127 통과 · **128 부터 거짓 진단**.
//
//   ☞ 이 저장소가 이미 적어 둔 그 모양이다 — *임의의 고정 배열이 조용히 자르고, 자르고
//     나면 진단이 사용자를 탓한다.* 두 겹의 거짓말이고, 사용자는 있지도 않은 짝 없는
//     괄호를 찾으러 간다(교훈 5: 진단은 **책임의 소재**를 말한다).
//
//   ★ 왜 여태 안 보였나: `check-limits` 가 «고정 표가 넘침을 말하는가» 를 재는데, 이 파일에
//     `LOW_HWM` 계측이 **하나도 없어서** 그 눈에 안 들어왔다.
//     ☞ *게이트가 재는 범위가 곧 그 게이트의 약속이다.*
//
//   ★★ **한 번만 말한다.** 깊이 300 이면 넘치는 자리가 172 곳이고, 172 줄을 뱉으면 그것은
//     진단이 아니라 소음이다. 첫 자리에서 말하고 그 뒤로는 조용히 넘긴다 — 다만 **버린다는
//     사실 자체는 이미 말했으므로** 뒤따르는 `E-PAREN-STRAY` 가 나도 거짓말이 아니다.
static void low_push_open(low_lexer_t *l, proven_u8 kind, proven_u32 line, proven_u32 col) {
    if (l->nopen < LOW_NEST_MAX) { l->open[l->nopen++] = kind; return; }
    if (!l->nest_told) {
        l->nest_told = true;
        low_diag(l, "E-NEST-DEPTH",
                 "nesting is deeper than this compiler tracks (128 levels of `(` and blocks "
                 "combined). The brackets here may well be balanced — the tool simply stopped "
                 "counting, and saying nothing would make the NEXT closer look unmatched. "
                 "Split the expression into named steps", line, col);
    }
}

// ── scanners ──────────────────────────────────────────────────────────────────

// word: [A-Za-z_][A-Za-z0-9_]*  with glued  .segment  extension for qualified names.
static proven_u8str_view_t low_scan_word(low_lexer_t *l) {
    proven_size_t start = l->pos;
    while (low_is_ident(low_peek(l))) low_adv(l);
    // ★★ 붙임 점 — **식별자 뒤의 빈칸 없는 `.` 은 접근이다.** 필드든 인덱스든.
    //   `h.kind` = 필드 · `arr.3` = 인덱스 · `g.row.2` = 섞어 쓴다.
    //
    //   ★ 전에는 **알파벳이 뒤에 올 때만** 붙였다. 그래서 `s.1` 은 `s` · `.`(형태 닫기) · `1`
    //     세 토큰이 됐고, `return s.1 .` 은 **`return s` 가 그 점에서 닫혀 버려** 슬라이스를
    //     그대로 냈다 — 인덱스가 **조용히 다른 뜻**이 됐다.
    //   ★ 숫자를 뺀 이유는 `.숫자` 가 **부동소수 소수부**이기 때문이다(`1.5`). 그런데 그건
    //     **앞 토큰이 숫자일 때만** 그렇다. **식별자 뒤의 `.숫자` 는 부동소수일 수 없다** —
    //     모호하지 않다. 그래서 붙인다.
    //   ★ 닫는 점과도 안 부딪친다: 닫는 점 뒤에는 **빈칸이나 줄바꿈**이 온다(`return arr.3 .`).
    while (low_peek(l) == '.' &&
           (low_is_alpha(low_peek_at(l, 1)) || low_is_digit(low_peek_at(l, 1)))) {
        low_adv(l);  // '.'
        while (low_is_ident(low_peek(l))) low_adv(l);
    }
    return low_span(l, start);
}

// ★★★ **자릿수 사이의 구분자** — 정본 §6.1.4 (2) · 부록 A.6 (4) (2026-08-31 수리).
//
//   규범은 `_` 가 **자릿수 사이에만** 온다고 적는다(`digit , { [ sep ] , digit }`). 그런데
//   훑기가 `while (자릿수 || '_')` 라 **위치를 안 봤다**: `1_` · `0x_1` · `0b_1` · `0x1_` 이
//   전부 통과했다. 규범과 구현이 갈린 채였고, 산문이 «자릿수 사이에» 라고만 적었을 때는
//   모호해서 잴 수도 없었다 — 부록 A.6 을 규칙으로 적고 나서야 드러났다.
//
//   ★ **왜 여기서 진단을 내는가.** 그냥 수를 끊고 파서에 맡기면 뒤따르는 `_` 가 이름이 되어
//     `E-IR-ARITY: extra operands` 가 난다(실측). 사용자는 **구분자를 잘못 쓴 것**인데
//     *"인자가 남는다"* 는 말을 듣는다 — 이 저장소가 교훈 5 로 적어 둔 **오진**이다.
//     ⇒ 그 자리에서 그 이름으로 문다.
//   ☞ *진단은 책임의 소재를 말한다. 틀린 자리를 짚으면 사람은 고칠 것이 없는 곳을 고친다.*
static bool low_scan_digits(low_lexer_t *l, bool (*isdig)(proven_byte_t),
                            proven_u32 line, proven_u32 col) {
    // ★ **첫 자리는 구분자일 수 없다.** 규칙이 `hexd , { [sep] , hexd }` 이므로 진법 표시
    //   바로 뒤(`0x_1`)나 소수점 바로 뒤에는 못 온다 — «사이» 가 아니라 «앞» 이기 때문이다.
    if (low_peek(l) == '_') {
        low_diag(l, "E-NUM-SEP",
                 "a digit separator `_` must sit BETWEEN digits — here it comes FIRST "
                 "(right after the base marker or the point), and there is no digit before it",
                 line, col);
        return false;
    }
    while (isdig(low_peek(l)) || low_peek(l) == '_') {
        if (low_peek(l) == '_' && !isdig(low_peek_at(l, 1))) {
            low_diag(l, "E-NUM-SEP",
                     "a digit separator `_` must sit BETWEEN digits — not at the start, "
                     "not at the end, and not right after the base marker "
                     "(`1_0` is fine; `1_`, `0x_1`, `0b1_` are not)", line, col);
            return false;
        }
        low_adv(l);
    }
    return true;
}

static bool low_is_bindig(proven_byte_t c) { return c == '0' || c == '1'; }

// ★★ **진법 표시 뒤에는 숫자가 하나 이상 온다** — 부록 A.6 `hexd , { [sep] , hexd }` (X-0066, 2026-09-26).
//
//   `low_scan_digits` 는 첫 자리의 `_` 만 보고 **숫자 0 개**는 그냥 통과시켰다. 그래서 `0x` · `0b` ·
//   `0X` · `0x.` 가 `check: ok` 이고 값 0 을 냈다 — 아무도 적지 않은 수가 0 이 됐다.
//   바깥(설정 포맷의 리터럴 차분 검사)에서 명세와 도구를 맞대 보다 드러났다.
//   ☞ `_` 는 여기서 거르지 않는다: `0x_1` 은 «숫자가 없다» 가 아니라 «나눔표가 앞에 왔다» 이고,
//     그 이름(`E-NUM-SEP`)은 `low_scan_digits` 가 이미 댄다. 한 잘못에 한 이름.
static bool low_need_digit(low_lexer_t *l, bool (*isdig)(proven_byte_t),
                           proven_u32 line, proven_u32 col) {
    if (isdig(low_peek(l)) || low_peek(l) == '_') return true;
    low_diag(l, "E-NUM-EMPTY",
             "a base marker (`0x` / `0b`) must be followed by at least one digit of that base — "
             "write `0x0` or `0b0` for zero",
             line, col);
    return false;
}

// ★ X-0067 — 지수 표시(`e` · `p`) 뒤에 숫자가 없다. 진법 표시 뒤에 숫자가 없는 것(X-0066)과
//   같은 잘못이므로 같은 이름(`E-NUM-EMPTY`)을 댄다. 한 잘못에 한 이름.
static void low_num_no_exp_digit(low_lexer_t *l, const char *mark, proven_u32 line, proven_u32 col) {
    (void)mark;
    low_diag(l, "E-NUM-EMPTY",
             "an exponent marker (`e` / `p`) must be followed by at least one digit, with an optional "
             "sign — write `1e3` or `0x1p4`, not `1e` or `0x1p`",
             line, col);
}
static bool low_is_ident_tail(proven_byte_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// number: dec | 0x-hex | 0b-bin | float (glued '.' fraction, optional exponent).
static void low_scan_number(low_lexer_t *l, proven_u32 line, proven_u32 col) {
    proven_size_t start = l->pos;
    if (low_peek(l) == '+' || low_peek(l) == '-') low_adv(l);  // optional sign (caller ensured a digit follows)
    if (low_peek(l) == '0' && (low_peek_at(l, 1) == 'x' || low_peek_at(l, 1) == 'X')) {
        low_adv(l); low_adv(l);
        if (!low_need_digit(l, low_is_hex, line, col)) return;
        if (!low_scan_digits(l, low_is_hex, line, col)) return;
        // hex float: '.' hexdigits and/or 'p'/'P' binary exponent (e.g. 0x1.8p3)
        if (low_peek(l) == '.' && low_is_hex(low_peek_at(l, 1))) {
            low_adv(l);
            if (!low_scan_digits(l, low_is_hex, line, col)) return;
        }
        if (low_peek(l) == 'p' || low_peek(l) == 'P') {
            proven_byte_t s1 = low_peek_at(l, 1);
            proven_size_t off = (s1 == '+' || s1 == '-') ? 2 : 1;
            if (low_is_digit(low_peek_at(l, off))) {
                low_adv(l);                          // p/P
                if (s1 == '+' || s1 == '-') low_adv(l);
                if (!low_scan_digits(l, low_is_digit, line, col)) return;
            } else {
                low_num_no_exp_digit(l, "p", line, col);   // ★ X-0067 — `0x1p` · `0x1p+`
                return;
            }
        }
    } else if (low_peek(l) == '0' && (low_peek_at(l, 1) == 'b' || low_peek_at(l, 1) == 'B')) {
        low_adv(l); low_adv(l);
        if (!low_need_digit(l, low_is_bindig, line, col)) return;
        if (!low_scan_digits(l, low_is_bindig, line, col)) return;
    } else {
        if (!low_scan_digits(l, low_is_digit, line, col)) return;
        // glued fraction: '.' followed by a digit
        if (low_peek(l) == '.' && low_is_digit(low_peek_at(l, 1))) {
            low_adv(l);  // '.'
            if (!low_scan_digits(l, low_is_digit, line, col)) return;
        }
        // exponent
        if (low_peek(l) == 'e' || low_peek(l) == 'E') {
            proven_byte_t s1 = low_peek_at(l, 1);
            proven_size_t off = (s1 == '+' || s1 == '-') ? 2 : 1;
            if (low_is_digit(low_peek_at(l, off))) {
                low_adv(l);                          // e/E
                if (s1 == '+' || s1 == '-') low_adv(l);
                if (!low_scan_digits(l, low_is_digit, line, col)) return;
            } else {
                low_num_no_exp_digit(l, "e", line, col);   // ★ X-0067 — `1e` · `1.5e` · `2e+`
                return;
            }
        }
    }
    // ★★ X-0067 (2026-09-27, 소유자 «새 오류 하나 + 기존 재사용») — **수 바로 뒤에 글자가 붙으면**
    //   그 자리에서 이름을 대고 멈춘다. 전에는 수를 거기서 끊고 나머지(`abc`·`u8`·`o7`·`g`·`2`)를
    //   다음 토큰으로 넘겨서, `12abc` 를 적은 사람이 «인자가 남는다»(`E-IR-ARITY`)는 말을 들었다 —
    //   교훈 5 의 오진이다. 부록 A.6 에는 접미사(`3u8`)도 팔진(`0o7`)도 없다.
    if (low_is_ident_tail(low_peek(l))) {
        low_diag(l, "E-NUM-SUFFIX",
                 "a number is glued to the letters or digits after it — this language has no number "
                 "suffixes (`3u8`), no octal (`0o7`), and a digit outside the base (`0b102`, `0xfg`) "
                 "does not belong to the number. Put a space between the number and the next word, "
                 "or fix the spelling of the number",
                 line, col);
        return;
    }
    // ★★★ **comptime 정수의 정밀도 하한을 여기서 지킨다** (RFC-0052 §8-5).
    //
    //   하한 = **[i64::MIN, u64::MAX] 를 정확히**. 그 밖의 크기는 이 언어가 컴파일타임에
    //   **표현할 수 없고**, 표현 못 하는 것을 조용히 넘기면 뒤에서 값이 지어내진다.
    //
    //   ☞ 실측으로 잡힌 것(2026-08-01): 리터럴 파서가 **둘**이었고 넘침을 **다르게** 다뤘다 —
    //     타입층 `num_value` 는 UINT64_MAX 로 **포화**하고(그래서 u64 에 "맞는다"고 통과),
    //     IR 의 `ir_int_lit` 는 2^64 로 **랩**했다. 그 결과 `99999999999999999999999` 가
    //     `--check` 초록이면서 실행값 200376420520689663 을 냈다. **같은 소스, 두 해석.**
    //     (main.c 의 CLI 인자에서 차등 퍼저가 찾아낸 것과 같은 종류의 버그다.)
    //
    //   ⇒ 자리를 **하나로** 만든다: 렉서가 크기를 재고 넘치면 여기서 거절한다. 그러면
    //     아래 두 파서는 넘치는 입력을 **볼 일이 없다**(그래도 각자 방어한다 — 심층 방어).
    //   ☞ 부동 리터럴·16진 부동은 이 규칙 밖이다(IEEE-754 가 자기 범위를 정의한다).
    {
        proven_u8str_view_t lx = low_span(l, start);
        bool isf = false;
        for (proven_size_t z = 0; z < lx.size; z++) {
            proven_byte_t ch = lx.ptr[z];
            if (ch == '.' || ch == 'p' || ch == 'P') { isf = true; break; }
            if ((ch == 'e' || ch == 'E') && !(lx.size > 1 && lx.ptr[0] == '0' &&
                (lx.ptr[1] == 'x' || lx.ptr[1] == 'X'))) { isf = true; break; }
        }
        if (!isf) {
            proven_size_t z = 0; proven_u64 base = 10, val = 0; bool ovf = false;
            if (z < lx.size && (lx.ptr[z] == '-' || lx.ptr[z] == '+')) z++;
            if (z + 1 < lx.size && lx.ptr[z] == '0' &&
                (lx.ptr[z + 1] == 'x' || lx.ptr[z + 1] == 'X')) { base = 16; z += 2; }
            else if (z + 1 < lx.size && lx.ptr[z] == '0' &&
                     (lx.ptr[z + 1] == 'b' || lx.ptr[z + 1] == 'B')) { base = 2; z += 2; }
            for (; z < lx.size && !ovf; z++) {
                proven_byte_t ch = lx.ptr[z];
                proven_u64 d;
                if (ch == '_') continue;
                if (ch >= '0' && ch <= '9') d = (proven_u64)(ch - '0');
                else if (base == 16 && ch >= 'a' && ch <= 'f') d = (proven_u64)(ch - 'a' + 10);
                else if (base == 16 && ch >= 'A' && ch <= 'F') d = (proven_u64)(ch - 'A' + 10);
                else break;
                if (d >= base) break;
                if (val > (UINT64_MAX - d) / base) ovf = true;
                else val = val * base + d;
            }
            if (ovf)
                low_diag(l, "E-LIT-RANGE",
                         "integer literal is too large to represent at compile time. The comptime "
                         "precision floor is EXACTLY [i64 min, u64 max] (RFC-0052 D3) — a literal "
                         "beyond it has no value this language can carry, so it is refused here "
                         "rather than silently wrapped or saturated into a different number",
                         line, col);
        }
    }
    low_emit(l, LOW_TOK_NUMBER, LOW_KW_NONE, low_span(l, start), LOW_EMPTY_VIEW, line, col);
}

// string: [prefix]"..."  with \" and \\ escapes. Emits the content span (quotes stripped).
//
// ★★ **접두 리터럴** (RFC-0035 D5): 낱말이 여는 따옴표에 **붙어** 있으면(공백 0) 그 낱말은
//    이름이 아니라 **리터럴의 접두사**다 — `u"AB"`·`u8"AB"`·`U"AB"`.
//    새 키워드 **0**: 접두사는 보통 낱말이고, 붙었는가 아닌가만이 뜻을 가른다.
//    ☞ 이 저장소의 다른 인접 규칙과 같은 결이다(`s.x` 접근 · `.f` 머리 · `s..m` 메서드):
//      **띄어쓰기가 문법이다.** `u "AB"` 는 여전히 이름 `u` 와 문자열 둘이다.
//    접두사가 무엇을 뜻하는지는 여기서 **판정하지 않는다** — 어휘는 한 곳(low_ir)에 산다.
static void low_scan_string_pfx(low_lexer_t *l, proven_u32 line, proven_u32 col,
                                proven_u8str_view_t prefix) {
    low_adv(l);  // opening quote
    proven_size_t content = l->pos;
    while (!low_at_end(l)) {
        proven_byte_t c = low_peek(l);
        if (c == '"') break;
        if (c == '\n') { low_diag(l, "E-STR-NEWLINE", "unterminated string (newline)", line, col); break; }
        if (c == '\\' && !low_at_end(l)) { low_adv(l); if (!low_at_end(l)) low_adv(l); continue; }
        low_adv(l);
    }
    proven_u8str_view_t body = { .ptr = l->src.ptr + content, .size = l->pos - content };
    if (low_peek(l) == '"') {
        low_adv(l);
        low_emit(l, LOW_TOK_STRING, LOW_KW_NONE, body, prefix, line, col);
    } else {
        low_diag(l, "E-STR-UNTERM", "unterminated string (EOF)", line, col);
        low_emit(l, LOW_TOK_ERROR, LOW_KW_NONE, body, LOW_EMPTY_VIEW, line, col);
    }
}

static void low_scan_string(low_lexer_t *l, proven_u32 line, proven_u32 col) {
    low_scan_string_pfx(l, line, col, LOW_EMPTY_VIEW);
}

// ★★★★★ **문자 리터럴** `'a'` (2026-08-07 · 소유자 결정).
//
//   실측이 먼저였다: `lib/regex.low` 이 자기 이스케이프 글자를 **십진수로** 적고 있었다
//   (`if eq c 100` = 'd' · `119` = 'w' · `115` = 's'), `lib/fmt.low` 은 **주석으로 글자를
//   날랐다**(`put_byte buf pos 45 . rem '-'`). 주석은 검사되지 않는다 — 리터럴은 검사된다.
//   ☞ `lib/term.low` 이 ESC 를 `27` 로 적던 것과 같은 자리이고, 그때 답이 `\xNN` 이었다.
//
//   ★ 값의 뜻은 **문자열 접두사 가족과 같다**(단계 Y): 접두사 없음·`u8` = u8 바이트 ·
//     `u` = UTF-16 코드 유닛 · `U` = 코드포인트. 한 칸에 안 들어가면 **거절**한다
//     (`u8'한'` 은 UTF-8 3 바이트 — 조용히 자르지 않는다).
//   ★ 새 키워드 **0**: `'` 는 여태 `E-CHAR`(뜻밖의 문자)였던 **빈 자리**다. `..` 가 메서드
//     문법에 쓰인 것과 같은 방식으로, 오류였던 자리를 뜻 있는 자리로 바꾼다.
//   ★ 렉서는 **판정하지 않는다** — 내용을 그대로 싣고 접두사를 aux 에 둔다. 값으로 접는 것은
//     low_ir 의 일이고, 그래야 어휘가 한 곳에 산다.
static void low_scan_char_pfx(low_lexer_t *l, proven_u32 line, proven_u32 col,
                              proven_u8str_view_t prefix) {
    low_adv(l);  // 여는 따옴표
    proven_size_t content = l->pos;
    while (!low_at_end(l)) {
        proven_byte_t c = low_peek(l);
        if (c == '\'') break;
        if (c == '\n') { low_diag(l, "E-CHAR-NEWLINE", "unterminated character literal (newline)", line, col); break; }
        if (c == '\\' && !low_at_end(l)) { low_adv(l); if (!low_at_end(l)) low_adv(l); continue; }
        low_adv(l);
    }
    proven_u8str_view_t body = { .ptr = l->src.ptr + content, .size = l->pos - content };
    if (low_peek(l) == '\'') {
        low_adv(l);
        low_emit(l, LOW_TOK_CHAR, LOW_KW_NONE, body, prefix, line, col);
    } else {
        low_diag(l, "E-CHAR-UNTERM", "unterminated character literal (EOF)", line, col);
        low_emit(l, LOW_TOK_ERROR, LOW_KW_NONE, body, LOW_EMPTY_VIEW, line, col);
    }
}

static void low_skip_line(low_lexer_t *l) {
    while (!low_at_end(l) && !low_is_nl(low_peek(l))) low_adv(l);
}

// note TERM \n … \n TERM  — terminator-style block comment (consumed, not emitted).
static void low_skip_note(low_lexer_t *l, proven_u32 line, proven_u32 col) {
    while (low_is_space(low_peek(l))) low_adv(l);
    proven_u8str_view_t term = low_scan_word(l);
    if (term.size == 0) { low_skip_line(l); return; }
    low_skip_line(l);
    if (!low_at_end(l)) low_adv(l);  // consume newline
    for (;;) {
        if (low_at_end(l)) { low_diag(l, "E-NOTE-UNTERM", "unterminated note block", line, col); return; }
        proven_size_t ls = l->pos;
        while (low_is_space(low_peek(l))) low_adv(l);
        proven_u8str_view_t w = low_scan_word(l);
        if (proven_u8str_view_eq(w, term)) return;  // closing terminator
        l->pos = ls; l->col = 1;  // (col approximate on multi-line skip)
        low_skip_line(l);
        if (!low_at_end(l)) low_adv(l);
    }
}

// text [proc] TERM \n … \n TERM  — raw text literal.
// ★★★ **닫는 줄의 규칙** (2026-10-07 · 소유자 결정).
//   ① 닫는 낱말은 **맨 첫 칸**에서 시작한다. 들여 쓴 태그는 닫는 낱말이 아니라 **본문**이다.
//   ② 그 뒤에는 줄바꿈이 바로 오거나, 빈칸들 뒤에 줄바꿈이 온다(파일 끝도 같다).
//   ③ 그 밖의 글자가 오면 거절한다(E-TEXTLIT-TAIL) — 문장을 닫는 `.` 은 **다음 줄**에 적는다.
//   ④ 닫는 줄 바로 앞의 줄바꿈 하나는 값에 들지 않는다(한 줄이면 줄바꿈 없는 문자열).
//   ☞ 전에는 «줄의 첫 낱말이 태그» 면 닫았다. 그래서 본문의 한 줄이 우연히 태그 낱말로 시작하면
//     거기서 조용히 끝났고, 들여 쓴 태그와 맨 첫 칸의 태그가 같은 뜻이었다.
static void low_scan_textlit(low_lexer_t *l, proven_u32 line, proven_u32 col) {
    while (low_is_space(low_peek(l))) low_adv(l);
    proven_u8str_view_t w1 = low_scan_word(l);
    proven_u8str_view_t proc = LOW_EMPTY_VIEW, term = w1;
    while (low_is_space(low_peek(l))) low_adv(l);
    if (low_is_alpha(low_peek(l))) {  // a second word on the line → proc + TERM
        proc = w1;
        term = low_scan_word(l);
    }
    if (term.size == 0) { low_diag(l, "E-TEXTLIT-TERM", "text literal missing terminator", line, col); return; }
    low_skip_line(l);
    if (!low_at_end(l)) low_adv(l);   // newline after opener
    proven_size_t body_start = l->pos;
    for (;;) {
        if (low_at_end(l)) {
            low_diag(l, "E-TEXTLIT-UNTERM", "unterminated text literal: the closing tag must start "
                     "in the FIRST column of its own line (an indented tag is part of the text)", line, col);
            low_emit(l, LOW_TOK_TEXTLIT, LOW_KW_NONE, low_span(l, body_start), proc, line, col);
            return;
        }
        // 여기는 언제나 줄의 맨 첫 칸이다. 태그 바이트가 그대로 있고 그 뒤가 이름 글자가 아니어야 닫는 낱말이다.
        proven_size_t ls = l->pos;
        bool closer = ls + term.size <= l->src.size;
        for (proven_size_t i = 0; closer && i < term.size; i++)
            if (l->src.ptr[ls + i] != term.ptr[i]) closer = false;
        if (closer && ls + term.size < l->src.size && low_is_ident(l->src.ptr[ls + term.size])) closer = false;
        if (closer) {
            // Drop the single newline immediately before TERM, so a one-line text literal is a
            // newline-free string; to keep a trailing newline, add one blank line before TERM.
            proven_size_t body_end = ls;
            if (body_end > body_start && l->src.ptr[body_end - 1] == '\n') {
                body_end--;
                if (body_end > body_start && l->src.ptr[body_end - 1] == '\r') body_end--;  // CRLF
            }
            proven_u8str_view_t body = { .ptr = l->src.ptr + body_start, .size = body_end - body_start };
            for (proven_size_t i = 0; i < term.size; i++) low_adv(l);
            while (low_is_space(low_peek(l))) low_adv(l);
            if (!low_at_end(l) && !low_is_nl(low_peek(l)))
                low_diag(l, "E-TEXTLIT-TAIL", "nothing may follow the closing tag of a text literal on its "
                         "line. Put the `.` that closes the statement on the NEXT line", l->line, l->col);
            low_emit(l, LOW_TOK_TEXTLIT, LOW_KW_NONE, body, proc, line, col);
            return;  // cursor is after TERM (and blanks); whatever follows is lexed normally
        }
        low_skip_line(l);
        if (!low_at_end(l)) low_adv(l);
    }
}

// ── driver ────────────────────────────────────────────────────────────────────
// ★★★ **소스는 well-formed UTF-8 이어야 한다** (RFC-0035). 잘못된 바이트열의 첫 오프셋을 준다(없으면 -1).
//   표준 UTF-8 검증: 선두 바이트 길이 · continuation(10xxxxxx) · overlong · surrogate · 범위(≤0x10FFFF).
static proven_size_t low_utf8_bad(const proven_byte_t *p, proven_size_t n) {
    // ★ 2026-10-02 — 손으로 짠 해독기(선두 길이·continuation·overlong·surrogate·범위)를 proven v0.6.0 의
    //   `proven_utf8_decode_next` 로 바꿨다. 같은 엄격한 규칙이고, 끝에서 잘린 글자(NEED_MORE)도 잘못된 것으로 본다.
    proven_u8str_view_t s = { .ptr = p, .size = n };
    for (proven_size_t i = 0; i < n; ) {
        proven_utf8_char_t c = proven_utf8_decode_next(s, i);
        if (c.err != PROVEN_OK) return i;
        i += c.len;
    }
    return (proven_size_t)-1;
}

low_lex_result_t low_lex(proven_allocator_t alloc, proven_u8str_view_t src) {
    low_lex_result_t out = { .ok = true };
    proven_result_array_t ta = PROVEN_ARRAY_INIT(alloc, low_token_t, 256);
    proven_result_array_t da = PROVEN_ARRAY_INIT(alloc, low_diag_t, 8);
    if (ta.err != PROVEN_OK || da.err != PROVEN_OK) { out.ok = false; return out; }
    out.tokens = ta.value;
    out.diags  = da.value;

    low_lexer_t l = { .src = src, .pos = 0, .line = 1, .col = 1, .out = &out };

    // ★★★ **잘못된 UTF-8 은 조용히 통과하지 않는다** (RFC-0035 D7·D8: 소스는 UTF-8).
    //   깨진 바이트는 오타가 아니라 **손상·오인코딩된 파일**이다 — 도구가 짐작으로 밀고 가면 안 된다.
    //   (유효 UTF-8 인 한국어 `rem` 주석·문자열은 그대로 통과한다 — 이건 *ill-formed* 만 잡는다.)
    {
        proven_size_t bad = low_utf8_bad(src.ptr, src.size);
        if (bad != (proven_size_t)-1) {
            proven_u32 ln = 1;
            for (proven_size_t q = 0; q < bad; q++) if (src.ptr[q] == '\n') ln++;
            low_diag(&l, "E-LEX-UTF8",
                     "the source is not well-formed UTF-8 — an invalid byte sequence. Lowent source is "
                     "UTF-8 (RFC-0035); a stray byte is not a typo the tool should guess through, it is a "
                     "corrupt or mis-encoded file.", ln, 0);
            return out;   // 렉싱 중단 — 바이트 스트림을 믿을 수 없다
        }
    }

    while (!low_at_end(&l)) {
        proven_byte_t c = low_peek(&l);
        if (low_is_space(c)) { low_adv(&l); continue; }
        // ★★★ **개행은 닫개가 아니다** (RFC-0103, 2026-08-27 소유자 결정).
        //   개행은 공백이다. 닫개는 오직 `.` 이다.
        //
        //   왜 없앴나: 개행 닫힘은 **줄바꿈을 의미로 만들어** SPEC-002 §2.3.4 가
        //   내세우는 것 — *"들여쓰기는 장식이다. 구조는 오직 닫개가 정한다 —
        //   붙여넣기·모바일·diff·LLM 생성에 강하다"* — 를 자기가 깎았다. 긴 줄을
        //   되감으면 프로그램이 바뀌었다. 조용한 오답이 한 번 났다(옛 §2.3.6, f(3,4)=3).
        //   그리고 이 규칙 하나가 주변에 군더더기를 길렀다: `,` 줄잇기 · CST 의
        //   `soft_close` 비트 · `low_dot_is_explicit()` · 수리 id 둘. 전부 같이 없앴다.
        //
        //   이행(실측): 코퍼스 351 개에 명시 닫개를 적고 나니, 이 규칙을 끈 사본에서
        //   빨강이 **정상과 정확히 같았고**(36) 골든 FAIL 이 128 → 2 였다. 남은 둘은
        //   이 규칙 자체를 시험하던 검사라 이 커밋에서 함께 없앴다.
        if (low_is_nl(c)) { low_adv(&l); continue; }

        proven_u32 line = l.line, col = l.col;

        if (low_is_alpha(c)) {
            proven_u8str_view_t w = low_scan_word(&l);
            if (proven_u8str_view_eq(w, PROVEN_LIT("rem")))  { low_skip_line(&l); continue; }
            if (proven_u8str_view_eq(w, PROVEN_LIT("note"))) { low_skip_note(&l, line, col); continue; }
            if (proven_u8str_view_eq(w, PROVEN_LIT("text"))) { low_scan_textlit(&l, line, col); continue; }
            // ★★ 낱말이 여는 따옴표에 **붙어** 있으면 이름이 아니라 **접두 리터럴**이다
            //    (RFC-0035 D5 · `u"AB"`). 공백이 하나라도 있으면 여전히 이름 + 문자열 둘이다.
            if (low_peek(&l) == '"') { low_scan_string_pfx(&l, line, col, w); continue; }
            if (low_peek(&l) == '\'') { low_scan_char_pfx(&l, line, col, w); continue; }
            low_kw_t kw = low_kw_lookup(w);
            low_emit(&l, LOW_TOK_IDENT, kw, w, LOW_EMPTY_VIEW, line, col);
            if (kw == LOW_KW_EXPR) l.in_expr = true;  // enter expr island (ends at its DOT)
            // ★ 블록을 여는 낱말: `do` 와, **`do` 없이 `end` 로 닫는 무두 블록**의 머리들
            //   (`struct N … end` · `enum` · `trait` · `contract` · `state`).
            // ★★★ **`actor` 는 뺐다** (2026-08-26 · 소유자 결정 S1). 두 뜻을 갖기 때문이다:
            //   선언 머리(`actor N … end`)와 **타입 표지**(`spawn actor T`). 렉서는 문맥이 없어
            //   둘을 못 가르고, 그래서 `(spawn actor T)` 의 `)` 가 블록 경계를 넘어
            //   `E-PAREN-STRAY` 가 났다 — 서식기가 낸 것을 서식기가 못 읽는 파일이 23 개였다.
            //   ☞ 그런데 `actor` 가 여기 있을 **필요가 없었다**: actor 몸통의 괄호 균형은
            //     안쪽 `proc … do … end` 가 이미 지킨다(실측: 괄호 탈출은 여전히
            //     `E-PAREN-ESCAPE` 로 잡힌다). **중복이 하나를 막고 있었다.**
            if (kw == LOW_KW_DO || kw == LOW_KW_STRUCT || kw == LOW_KW_ENUM ||
                kw == LOW_KW_TRAIT || kw == LOW_KW_CONTRACT ||
                kw == LOW_KW_STATE) {
                low_push_open(&l, LOW_OPEN_BLOCK, line, col);
            } else if (kw == LOW_KW_END) {
                // ★★ **괄호는 `end` 를 넘어갈 수 없다.** 넘어가려 하면 그 자리에서 닫는다 —
                //   피해를 **그 블록 안에 가둔다**. 안 그러면 파일의 나머지가 통째로 죽는다.
                while (l.nopen && l.open[l.nopen - 1] == LOW_OPEN_PAREN) {
                    l.nopen--; if (l.paren > 0) l.paren--;
                    low_diag(&l, "E-PAREN-ESCAPE",
                             "a `(` opened inside this block was never closed — it would have "
                             "ESCAPED past the `end`. It used to: the newline-continuation stayed on "
                             "for the REST OF THE FILE and every op after it was swallowed into the "
                             "group. A bracket may not cross a block boundary; it is closed here",
                             line, col);
                }
                if (l.nopen) l.nopen--;
            }
            continue;
        }
        if (low_is_digit(c)) { low_scan_number(&l, line, col); continue; }
        if (c == '"') { low_scan_string(&l, line, col); continue; }
        if (c == '\'') { low_scan_char_pfx(&l, line, col, LOW_EMPTY_VIEW); continue; }

        switch (c) {
            case '.':
                // ★★★ **`..name` — 수신자 우선 머리** (RFC-0062).
                //
                //   `s..area 2`  ≡  `rect.area s 2`   (s 의 타입이 rect 일 때)
                //
                //   한 점과 두 점이 **데이터와 적용**을 가른다:
                //       s.w        **필드 접근** — 값이다
                //       s..area    **메서드 호출** — 적용이다
                //
                //   ★ `..` 는 지금까지 **오류**였다(E-DOT-DOUBLE) — 즉 **비어 있는 자리**였다.
                //     새 특수문자를 안 늘리고(P6) 그 자리를 쓴다.
                // ★★★★★ **`..` 는 폐기됐다** (2026-08-25 · 소유자 결정).
                //   메서드 호출은 이제 **전위 하나뿐**이다: `method <값> <마디…> <이름> <인자…> .`
                //   ★ 왜 없애나: `..` 는 **오른쪽에서 왼쪽으로 읽히는 유일한 자리**였고
                //     (`s..area` = `rect.area s`), 다단 수신자를 적을 방법이 없었다.
                //     `field` 를 다단 전위로 넓히고 나니 메서드만 중위로 남을 이유가 없다 —
                //     **한 뜻에 한 철자**(같은 규율이 붙임점 필드 접근을 없앤 이유다).
                //   ★ 토큰 종류(`LOW_TOK_METHOD`)는 **남긴다**: 전위 `method` 가 사슬을
                //     내릴 때 그 자리를 합성해 쓴다. 없애는 것은 **표기**이지 기계가 아니다.
                if (low_peek_at(&l, 1) == '.' && low_is_alpha(low_peek_at(&l, 2))) {
                    low_adv(&l); low_adv(&l);
                    proven_size_t ms = l.pos;
                    while (low_is_ident(low_peek(&l))) low_adv(&l);
                    low_diag(&l, "E-DOT-DOUBLE",
                             "`recv..op` is gone — write `method <recv> <name> <args…>` instead. "
                             "One meaning gets one spelling, and the prefix form is the only one "
                             "that can name a MULTI-LEVEL receiver (`method o inner deep area`).",
                             line, col);
                    (void)ms;
                }
                else if (low_peek_at(&l, 1) == '.') {
                    low_adv(&l); low_adv(&l);
                    low_diag(&l, "E-DOT-DOUBLE", "'..' is not a token (a method call is `recv..op`)", line, col);
                    low_emit(&l, LOW_TOK_ERROR, LOW_KW_NONE, LOW_EMPTY_VIEW, LOW_EMPTY_VIEW, line, col);
                }
                // ★★★ **`.name` — 머리를 연다** (2026-07-14, 사용자 제안).
                //
                //   `.` 이 **이름에 붙어** 있고(오른쪽) 왼쪽은 공백이면 그것은 **머리 표시**다.
                //   왼쪽이 붙어 있으면(`s.x`) 그건 **접근**이고, 이미 낱말 스캐너가 먹는다 —
                //   그래서 여기까지 오는 `.` 은 **언제나 왼쪽이 고립**돼 있다(§2.4a).
                //
                //   ⇒ 세 뜻이 **인접성으로 결정적으로 갈린다**:
                //        `x . y`  닫개      (양쪽 공백)
                //        `s.x`    접근      (양쪽 붙음)   ← 낱말 스캐너가 처리
                //        `.f`     머리 열기 (오른쪽만 붙음)
                //
                //   ★ 이것이 사 주는 것: **머리/원자 비트가 문자열 안에 있다.** 조각을 혼자
                //     읽을 수 있고, 선언을 몰라도 무엇이 호출인지 보인다. 그리고 **핸드폰에서
                //     치기 쉽다**(괄호 두 개 대신 점 하나).
                else if (low_is_alpha(low_peek_at(&l, 1))) {
                    low_adv(&l);                       // '.'
                    proven_size_t s0 = l.pos;
                    while (low_is_ident(low_peek(&l))) low_adv(&l);
                    proven_u8str_view_t nm = { .ptr = l.src.ptr + s0, .size = l.pos - s0 };
                    // ★★★ RFC-0113 R3 (소유자 2026-10-02 «없앤다») — `.name` 은 셋째 철자였다(맨 전위 · 괄호가 같은 나무를 세운다).
                    //   띄어쓰기 한 칸이 닫개를 머리로 바꿨다(`add a b .g a` → `.g` 가 머리). 거절하되 나무는 그대로 세워 뒤 진단이 이어지게 한다.
                    low_diag(&l, "E-VOCAB-REMOVED",
                             "`.name` (a dot glued to a name opens a head) is gone — write the op name plainly or in "
                             "parentheses: `return add a (mul b 2) .` (RFC-0113 R3). A glued dot after a space was too easy "
                             "to type by accident: `add a b .g a` turned the closer into a head", line, col);
                    low_emit(&l, LOW_TOK_HEAD, low_kw_lookup(nm), nm, LOW_EMPTY_VIEW, line, col);
                }
                else {
                    low_adv(&l);
                    low_emit(&l, LOW_TOK_DOT, LOW_KW_NONE, PROVEN_LIT("."), LOW_EMPTY_VIEW, line, col);
                    l.in_expr = false;  // the island's closer
                }
                break;
            // ★★ `,` 는 **R3(쉼표)** 였다 — 폼을 닫지 않고 같은 폼 안에서 다음 인자를
            //   여는 낱말. 규칙 자체는 멀쩡했고 기계 증명(`comma_does_not_close`)도 있었다.
            //   없앤 이유는 **아무도 안 썼기 때문**이다: 저장소 전체에서 COMMA 토큰이 셋
            //   뿐이었고 **셋 다 줄잇기**였다(RFC-0103 ⓓ 에서 없앤 역할). R3 이 내세우는
            //   인자 구분자 용법은 한 자리도 없었다. 그 셋에서 `,` 를 지워도 **def 해시가
            //   그대로**였다 — 아무 뜻도 안 나르고 있었다.
            //   ⇒ 파싱은 되는데 효과가 없는 낱말은 **조용한 함정**이다. 그래서 오류로 만든다.
            //   (`;` 가 바로 아래에서 같은 이유로 죽었다. 같은 병, 같은 처방.)
            case ',': low_adv(&l);
                low_diag(&l, "E-VOCAB-REMOVED",
                         "`,` (R3) was removed — RFC-0103, 2026-08-27. It opened the NEXT operand of "
                         "the same form, but nothing used it that way: every `,` in the corpus was a "
                         "LINE CONTINUATION, and newlines no longer close a form, so continuing a "
                         "line needs nothing at all. A form is `head operand*` and ends at its "
                         "closer `.` — just write the operands, on as many lines as you like.",
                         line, col);
                break;
            // ★ `;` 는 **`.` 의 세 번째 철자**였다 — 렉서가 그냥 DOT 을 냈다. 닫개가
            //   `.` · 개행 · `;` 셋이었다. §2.5 는 동의어를 금지한다. 그리고 그것이 살아
            //   있던 유일한 이유는 **지운 두 번째 언어**(`let a 6 ; let b 7`)였다.
            //   특수문자는 최소로(P6): 같은 줄에 두 문장을 쓰려면 **`.` 을 쓴다.**
            case ';': low_adv(&l);
                low_diag(&l, "E-VOCAB-REMOVED",
                         "`;` was a THIRD spelling of the closer `.` (the lexer literally emitted a "
                         "DOT for it) — one meaning, three spellings, and SPEC-002 §2.5 forbids "
                         "synonyms. It only survived inside the old interpreter's second language. "
                         "Write `.`", line, col);
                break;
            case '(': low_adv(&l); low_emit(&l, LOW_TOK_LPAREN, LOW_KW_NONE, PROVEN_LIT("("), LOW_EMPTY_VIEW, line, col);
                low_push_open(&l, LOW_OPEN_PAREN, line, col);
                l.paren++; break;
            case ')': low_adv(&l); low_emit(&l, LOW_TOK_RPAREN, LOW_KW_NONE, PROVEN_LIT(")"), LOW_EMPTY_VIEW, line, col);
                if (l.nopen && l.open[l.nopen - 1] == LOW_OPEN_PAREN) { l.nopen--; if (l.paren > 0) l.paren--; }
                else low_diag(&l, "E-PAREN-STRAY",
                              "a `)` with no matching `(` — or one that would cross a block boundary. "
                              "Brackets and blocks must NEST, not interleave", line, col);
                break;
            case '+': case '-':
                // outside an expr island, `-`/`+` glued to a digit is a signed literal
                // (there is no infix `-` in argument position); inside expr it's the operator
                if (!l.in_expr && low_is_digit(low_peek_at(&l, 1))) { low_scan_number(&l, line, col); break; }
                [[fallthrough]];
            case '*': case '/': {
                proven_size_t s = l.pos; low_adv(&l);
                low_emit(&l, LOW_TOK_OP, LOW_KW_NONE, low_span(&l, s), LOW_EMPTY_VIEW, line, col);
                break;
            }
            default:
                low_adv(&l);
                // ★★★ **비-ASCII 는 여기서만 특별하다** (RFC-0035 §8-4 — 식별자는 ASCII).
                //   소스는 UTF-8 이고 **문자열·주석은 어떤 글자든 담는다**(이 저장소의 주석이
                //   그 증거다). 다만 **이름**은 ASCII 다. 그 결정을 "unexpected character" 라고만
                //   말하면 사용자는 파일이 깨진 줄 안다 — 규칙이지 사고가 아니라고 말해야 한다.
                if (c >= 0x80) {
                    while ((low_peek(&l) & 0xC0) == 0x80) low_adv(&l);   // 이어지는 바이트까지 한 글자로
                    low_diag(&l, "E-NAME-ASCII",
                             "identifiers are ASCII. The source file itself is UTF-8 and strings and "
                             "`rem` comments hold any script you like — but a NAME is ASCII, so that "
                             "two names can never differ by something invisible (normalization forms, "
                             "confusable letters, bidi marks). Write the identifier in ASCII and put "
                             "the other script in a comment or a string (RFC-0035)",
                             line, col);
                    low_emit(&l, LOW_TOK_ERROR, LOW_KW_NONE, LOW_EMPTY_VIEW, LOW_EMPTY_VIEW, line, col);
                    break;
                }
                low_diag(&l, "E-CHAR", "unexpected character", line, col);
                low_emit(&l, LOW_TOK_ERROR, LOW_KW_NONE, low_span(&l, l.pos - 1), LOW_EMPTY_VIEW, line, col);
                break;
        }
    }
    low_emit(&l, LOW_TOK_EOF, LOW_KW_NONE, LOW_EMPTY_VIEW, LOW_EMPTY_VIEW, l.line, l.col);
    // ★ 파일 끝에 남은 괄호 — 말한다. (블록의 `end` 누락은 파서가 잡는다.)
    while (l.nopen) {
        if (l.open[l.nopen - 1] == LOW_OPEN_PAREN)
            low_diag(&l, "E-PAREN-UNCLOSED", "a `(` was never closed", l.line, l.col);
        l.nopen--;
    }
    return out;
}
