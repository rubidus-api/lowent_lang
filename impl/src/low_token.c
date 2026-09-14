#include "low_token.h"

typedef struct { const char *word; low_kw_t kw; } low_kwent_t;

// Reserved words classified onto IDENT. rem/note/text are consumed structurally by
// the lexer and never reach here.
//
// ★★ 2026-07-13 — **여섯 낱말을 없앴다**(사용자 결정). 어휘는 작고 직교해야 한다(P1),
//   그리고 SPEC-002 §2.5 는 **동의어를 금지한다.** 그런데 여섯이 그 규칙을 어기고 있었다:
//     is    — **장식이었다.** 어떤 코드도 검사하지 않았다: `type h zzz u8 .` 이 통과했다.
//     as    — 세 자리 전부 비하중: `use M as N`(강제 안 됨) · `do as <label>`(파서가 **버렸다**)
//     give  — **`return` 의 순수 동의어**였다(둘 다 같은 값을 냈다). 명세는 "블록 값(꼬리)"
//             이라 했지만 **블록이 값이 아니라서** 뜻이 겹쳤다. 수식 지향 블록(RFC-0019)을
//             실제로 넣을 때 **그때** 다시 들이면 된다.
//     fn    — 옛 람다. S5 코어에 없다.
//     fail  — **명세 어휘 목록 어디에도 없다.**
//     unit  — 명세는 **`void`** 라고 부른다. 하나의 뜻에 두 철자(교훈 7).
//
//   ★ 여섯이 살아 있던 **유일한 이유**는 `low_eval.c`(옛 트리워킹 인터프리터)와 그것이 돌리던
//     **명세에 없는 두 번째 언어**(`print`·`record`·`list`·`map`·`;`·`be` 없는 `let`)였다.
//     그 인터프리터를 지웠다. **언어는 하나여야 한다.**
//
//   ★★ 없앤 낱말은 **조용히 IDENT 로 재해석되면 안 된다** — `type h is u8 .` 의 `is` 가
//     타입 이름으로 읽히면 최악이다(조용히 틀린 답). `low_check` 가 **명시적으로 거절한다**
//     (`E-VOCAB-REMOVED`), 그리고 무엇으로 바꿔 쓰라고 **말한다**.
static const low_kwent_t LOW_KW_TABLE[] = {
    {"use", LOW_KW_USE},   {"let", LOW_KW_LET},
    {"set", LOW_KW_SET},   {"do", LOW_KW_DO},
    {"end", LOW_KW_END},   {"if", LOW_KW_IF},       {"else", LOW_KW_ELSE},
    {"for", LOW_KW_FOR},
    {"guard", LOW_KW_GUARD}, {"return", LOW_KW_RETURN},
    {"break", LOW_KW_BREAK}, {"continue", LOW_KW_CONTINUE},
    {"try", LOW_KW_TRY},   {"expr", LOW_KW_EXPR},
    {"true", LOW_KW_TRUE}, {"false", LOW_KW_FALSE}, {"none", LOW_KW_NONEVAL},
    // MVP (S3)
    {"fn", LOW_KW_FN}, {"proc", LOW_KW_PROC}, {"make", LOW_KW_MAKE},   // ★ RFC-0082 (전 calcop/procop)
    {"struct", LOW_KW_STRUCT}, {"enum", LOW_KW_ENUM}, {"match", LOW_KW_MATCH},
    {"case", LOW_KW_CASE}, {"while", LOW_KW_WHILE}, {"module", LOW_KW_MODULE},
    {"var", LOW_KW_VAR}, {"type", LOW_KW_TYPE}, {"newtype", LOW_KW_NEWTYPE}, {"export", LOW_KW_EXPORT},
    {"unsafe", LOW_KW_UNSAFE}, {"extern", LOW_KW_EXTERN},
    {"trait", LOW_KW_TRAIT}, {"satisfies", LOW_KW_SATISFIES},
    {"contract", LOW_KW_CONTRACT}, {"drop", LOW_KW_DROP},
    {"test", LOW_KW_TEST}, {"expect", LOW_KW_EXPECT},
    // ★★ `on` 을 **없앴다**(2026-07-14). 액터 안의 op 은 **어차피 핸들러다** — `on` 은 잉여였고,
    //   더 나쁘게 **순수/절차 1비트를 우회했다**: IR 이 `is_calc = (kw != PROCOP)` 으로 정하는데
    //   `on` 은 PROCOP 이 아니므로 **순수(calc)로 취급됐다.** 그래서 액터 상태를 **고쳐 쓰는**
    //   핸들러가 `effects none` 을 선언하고 **아무 말도 안 들었다** —
    //   `fn` 이 남의 버퍼를 고치면 E-EFFECT-PURITY 로 잡는데(RFC-0057), 여기선 안 봤다.
    //   RFC-0046 이 "순수/절차 1비트 항상 명시" 라며 없앤 `op` 기본값이, `on` 으로 되살아나 있었다.
    //   ⇒ 액터 안에서도 `fn`/`proc` 을 쓴다. **1비트가 돌아온다.**
    {"actor", LOW_KW_ACTOR}, {"state", LOW_KW_STATE},
    {"spawn", LOW_KW_SPAWN}, {"send", LOW_KW_SEND},   // `to` 는 이미 어휘에 있다
    {"be", LOW_KW_BE},
};

low_kw_t low_kw_lookup(proven_u8str_view_t word) {
    for (proven_size_t i = 0; i < sizeof LOW_KW_TABLE / sizeof LOW_KW_TABLE[0]; i++) {
        if (proven_u8str_view_eq(word, proven_u8str_view_from_cstr(LOW_KW_TABLE[i].word))) {
            return LOW_KW_TABLE[i].kw;
        }
    }
    return LOW_KW_NONE;
}

const char *low_tok_kind_name(low_tok_kind_t k) {
    switch (k) {
        case LOW_TOK_EOF:     return "EOF";
        case LOW_TOK_IDENT:   return "IDENT";
        case LOW_TOK_NUMBER:  return "NUMBER";
        case LOW_TOK_STRING:  return "STRING";
        case LOW_TOK_CHAR:    return "CHAR";   // ★ 이름표는 **한 벌** — 열거만 늘리면 `?` 가 된다
        case LOW_TOK_HEREDOC: return "HEREDOC";
        case LOW_TOK_DOT:     return "DOT";
        case LOW_TOK_HEAD:    return "HEAD";   // `.name` — 머리를 연다
        case LOW_TOK_METHOD:  return "METHOD"; // `..name` — 수신자 우선 머리
        case LOW_TOK_LPAREN:  return "LPAREN";
        case LOW_TOK_RPAREN:  return "RPAREN";
        case LOW_TOK_OP:      return "OP";
        case LOW_TOK_ERROR:   return "ERROR";
    }
    return "?";
}
