// low_token.h — token & keyword definitions for the lowent point-closure surface.
// Shared front-end (SPEC-MINI §2 / SPEC-002 부록 G, RFC-0046 rev.d).
#ifndef LOW_TOKEN_H
#define LOW_TOKEN_H

#include "proven/types.h"
#include <stdlib.h>   // strtod — low_num_to_double
#include "proven/u8str.h"

// Token kinds. The point-closure surface is deliberately tiny at L0/L1:
// words, literals, and the structural punctuation (closers, group, comma).
typedef enum {
    LOW_TOK_EOF = 0,
    LOW_TOK_IDENT,    // word or glued-dot qualified name (net.http); may be a keyword (see .kw)
    LOW_TOK_NUMBER,   // dec/hex/bin/float lexeme (glued '.' = float fraction)
    LOW_TOK_STRING,   // "..."  (.lex = content between quotes, unescaped span)
    // ★★★★★ `'a'` — **문자 리터럴** (2026-08-07). `.lex` = 따옴표 사이 원문 ·
    //   `.aux` = 접두사(없음·`u8`·`u`·`U` — 문자열 접두사와 **같은 어휘**).
    //   값으로 접는 것은 low_ir 의 일이다: 렉서는 판정하지 않는다.
    LOW_TOK_CHAR,
    LOW_TOK_HEREDOC,  // text [proc] TERM ... TERM  (.lex = body, .aux = proc name)
    LOW_TOK_DOT,      // isolated '.'  = form/statement closer
    LOW_TOK_METHOD,   // ★ '..name' = **수신자 우선 머리**: `s..area x` ≡ `<s의 타입>.area(s, x)`
    LOW_TOK_HEAD,     // ★ '.name' (점이 이름에 **붙어** 있고 왼쪽은 공백) = **머리를 연다**
                      //   .lex = 이름. 붙임 점(`s.x`, 접근)과 구별된다: 그것은 **왼쪽이 붙어** 있다.
    LOW_TOK_LPAREN,   // '('          = decoration group open (R5)
    LOW_TOK_RPAREN,   // ')'
    LOW_TOK_OP,       // + - * /      = arithmetic infix (only meaningful inside expr)
    LOW_TOK_ERROR,    // lexical error span (e.g. "..", bad escape)
} low_tok_kind_t;

// Reserved words. rem/note/text are handled structurally by the lexer (comments,
// heredoc) and never surface as IDENT tokens; the rest are classified onto IDENT.
typedef enum {
    LOW_KW_NONE = 0,
    LOW_KW_USE, LOW_KW_AS, LOW_KW_LET, LOW_KW_SET,
    LOW_KW_DO, LOW_KW_END, LOW_KW_IF, LOW_KW_ELSE, LOW_KW_FOR, LOW_KW_IN, LOW_KW_TO,
    LOW_KW_LOOP, LOW_KW_GUARD, LOW_KW_RETURN, LOW_KW_GIVE, LOW_KW_BREAK, LOW_KW_CONTINUE,
    LOW_KW_TRY, LOW_KW_FAIL, LOW_KW_EXPR, LOW_KW_FN,   // ★ RFC-0082: fn = 순수 함수 정의(전 calcop; 전 람다 fn 은 폐기)
    LOW_KW_TRUE, LOW_KW_FALSE, LOW_KW_NONEVAL, LOW_KW_UNIT,  // NONEVAL = the `none` literal (distinct from the sentinel LOW_KW_NONE=0)
    // MVP front-end (S3) — shared parser grows additively (BOOTSTRAP §3)
    LOW_KW_PROC, LOW_KW_MAKE, LOW_KW_STRUCT, LOW_KW_ENUM,
    LOW_KW_MATCH, LOW_KW_CASE, LOW_KW_WHILE, LOW_KW_MODULE, LOW_KW_VAR,
    LOW_KW_TYPE, LOW_KW_NEWTYPE, LOW_KW_EXPORT, LOW_KW_BE,
    // ★ SPEC-003 §21 의 수식자. `export` 만 어휘에 있었다 — 나머지 셋은 **평평하게**
    //   파싱돼 선언을 **조각냈다**(op 이 통째로 사라진다).
    LOW_KW_UNSAFE, LOW_KW_LOCAL, LOW_KW_EXTERN,
    // ★ SPEC-003 §60: "인터페이스 = trait … `satisfies` 로 충족 확인."
    //   둘 다 **어휘에 없었다** — `satisfies nonexistent_trait .` 이 조용히 통과했다.
    LOW_KW_TRAIT, LOW_KW_SATISFIES,
    // ★ `test N do … end` 은 **죽은 문법이었다** — 파싱되고 절대 실행되지 않았다.
    //   테스트를 썼는데 안 돌아간다: 초록불이 "검사됐다" 가 아니라 "아무도 안 돌렸다" 였다.
    //   살리려면 **무엇으로 단언하는가**가 있어야 한다. `expect <cond> .` 이 그것이다.
    LOW_KW_TEST, LOW_KW_EXPECT,
    // ★ level 2 — actor (SPEC-008 §9.4). LowentDRF.v 가 **무엇을 주는지 이미 증명**했다
    //   (소유가 격리되고 이전이 메시지로만 일어나면 경합이 없다). 도구가 안 주고 있었다:
    //   `actor` 안의 op 들이 **IR 에서 통째로 사라졌다**.
    LOW_KW_ACTOR, LOW_KW_STATE, LOW_KW_ON, LOW_KW_SPAWN, LOW_KW_SEND,
    // ★ 명명 계약(SPEC-002 §267). 어휘에 없어서 **평평하게 파싱돼 선언이 조각났다.**
    LOW_KW_CONTRACT,
    // ★ SPEC-004 §4.8 은 `drop` 을 규정해 두고도 **어휘에 없었다** — `E-IR-UNDEF`(오진).
    LOW_KW_DROP,
} low_kw_t;

typedef struct {
    low_tok_kind_t      kind;
    low_kw_t            kw;    // LOW_KW_NONE unless an IDENT that is a reserved word
    proven_u8str_view_t lex;   // slice into the source buffer (zero-copy)
    proven_u8str_view_t aux;   // heredoc proc name; empty otherwise
    proven_u32          line;  // 1-based
    proven_u32          col;   // 1-based (byte column)
} low_token_t;

// Classify a word lexeme as a reserved keyword, or LOW_KW_NONE.
[[nodiscard]] low_kw_t low_kw_lookup(proven_u8str_view_t word);

// Human-readable token-kind name (for dumps/diagnostics).
[[nodiscard]] const char *low_tok_kind_name(low_tok_kind_t k);

// ★★★★★ **부동 리터럴을 값으로 — 한 벌** (2026-08-07).
//
//   자릿수 구분자 `_` 는 정수 경로에서만 걸러지고 **부동 경로에서는 안 걸러졌다**:
//   `strtod("1_0.5")` 는 `_` 에서 멈춰 **1.0** 을 낸다. 렉서는 `_` 를 옳게 먹어 토큰에 담았고,
//   값으로 접는 쪽이 그것을 몰랐다. 실측(2026-08-07):
//
//       1_0.5   → 1.0   (10.5 여야 한다)
//       10.2_5  → 10.2  (10.25)
//       1e1_0   → 10.0  (1e10)
//
//   ★ 거절도 경고도 없는 **조용한 오답**이다 — 이 저장소가 가장 싫어하는 부류.
//   ☞ 그리고 이 파일의 정수 쪽 주석이 이미 같은 병을 적어 두었다: *"리터럴 파서가 둘이었고
//     넘침을 다르게 다뤘다 … 같은 소스, 두 해석"*. 정수는 한 벌로 묶였는데 **부동은 안 묶였고**,
//     그래서 `low_ir.c` 와 `low_typecheck.c` 가 **각자 strtod 를 불렀다.**
//   ⇒ 여기 한 벌을 둔다. 둘이 이걸 부르면 갈릴 수가 없다.
static inline double low_num_to_double(proven_u8str_view_t v) {
    char buf[64];
    proven_size_t n = 0;
    for (proven_size_t i = 0; i < v.size && n < 63; i++)
        if (v.ptr[i] != (proven_byte_t)'_') buf[n++] = (char)v.ptr[i];
    buf[n] = 0;
    return strtod(buf, NULL);
}

#endif // LOW_TOKEN_H
