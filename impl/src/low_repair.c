// low_repair.c — **코드별 수리 레지스트리** (REQ-0003 · 후속 I, 2026-08-04).
//
// ★★★ 규율 셋:
//   ① **코드가 수리를 결정할 때만** 여기 적는다. 문맥이 있어야 정해지는 것은 `NOREPAIR` 로
//      내리고 **왜 없는지**를 적는다 — 지어낸 수리는 없는 수리보다 나쁘다(읽는 쪽이 AI 라
//      그것을 그대로 실행한다).
//   ② 방출 자리가 `d->repair` 를 채웠으면 **그쪽이 이긴다.** 표는 나머지를 맡는다.
//   ③ 여기 없는 코드는 **없는 것이 아니라 분류가 빠진 것**이다 —
//      `scripts/check-repair.py` 가 컴파일러의 코드 전체와 양방향으로 대고 빨간불을 낸다.
//
// ☞ 수리 id 는 **안정 식별자**다. 문구가 아니라 이 id 가 계약이다. 여러 코드가 같은 id 를
//   가리키는 것은 정상이다 — 수리는 *종류*이지 코드마다 하나씩 있는 것이 아니다.
#include "low_repair.h"
#include <string.h>

typedef struct { const char *code; const char *repair; } low_repair_row_t;

// ── 수리가 코드로 정해지는 것들 ─────────────────────────────────────────────
static const low_repair_row_t REPAIR[] = {
    // 권한 — "무엇을 받아라"
    { "E-ALLOC-NOCAP",         "R-ADD-CAP-ALLOC" },
    { "E-ASM-NOCAP",           "R-ADD-CAP-MACHINE" },
    { "E-MMIO-NOCAP",          "R-ADD-CAP-MACHINE" },
    { "E-RESERVE-NOCAP",       "R-ADD-CAP-MACHINE" },
    { "E-FFI-NOCAP",           "R-ADD-CAP-C" },
    { "E-FFI-CAPKIND",         "R-ADD-CAP-C" },
    // ★ 호스트 잎에 **종류가 다른** 권한을 줬다 — 고침은 하나뿐이다: 그 잎이 이름하는 권리를 받는다.
    { "E-CAP-KIND",            "R-ADD-CAP" },
    { "E-CAP-MISSING",         "R-ADD-CAP" },
    // ★ RFC-0112 (WO-0211–0222) 에서 생긴 코드 — 2026-09-14 t4 게이트가 «분류 안 됨» 으로 잡았다(그 사이 t3 까지만 돌았다).
    { "E-CAP-FORGE",           "R-ADD-CAP" },            // 권한 칸 actor 를 띄우는 op 이 그 종류의 권한을 받는다
    { "E-ALLOC-CAPACITY-MARK", "R-ADD-CAPACITY-MARK" },  // 크기 앞에 `capacity` 를 적는다
    { "E-ALLOC-AMBIGUOUS",     "R-ADD-USING" },          // 바인딩에 `using <출처>` 를 적어 고른다(후보는 진단이 말한다)
    { "E-ALLOC-USING-UNUSED",  "R-DROP-USING" },         // 얼로케이터를 안 받는 호출이다 — 바인딩의 `using` 을 지운다
    { "E-USING-DUP",           "R-DROP-DUP-CLAUSE" },
    { "E-ENUM-DOT",            "R-CLOSE-VARIANT" },      // 갈래마다 `.` 을 붙인다   // ★ WO-0221 — 잎에 권한을 안 댔다: 받은 권한을 첫 피연산자로 적는다
    { "E-EFFECT-NO-CAP",       "R-ADD-CAP" },
    { "E-ENTRY-CAP",           "R-USE-SUPPORTED-CAP" },
    { "E-FN-CAP",              "R-DROP-CAP-PARAM" },

    // 영역 — "여덟 중 하나를 골라라"
    // ★ `region <이름> <종류>` 의 종류는 닫힌 어휘 여덟이다(2026-08-31, 원장 N-03).
    //   고침은 하나뿐이다: 여덟 중 뜻이 맞는 것으로 바꾼다. 그래서 수리가 있다.
    { "E-REGION-KIND",         "R-USE-REGION-KIND" },

    // 효과 — "무엇을 적어라 / 지워라"
    { "E-ASM-NOEFFECT",        "R-DECLARE-EFFECT-UNSAFE" },
    { "E-FFI-NOEFFECT",        "R-DECLARE-EFFECT-UNSAFE" },
    { "E-MMIO-NOEFFECT",       "R-DECLARE-EFFECT-DEVICE" },
    { "E-ISR-EFFECT",          "R-DECLARE-EFFECT-DEVICE" },
    { "E-RESERVE-NOEFFECT",    "R-DECLARE-EFFECT-STATE" },
    { "E-UNSAFE-UNDECLARED",   "R-MARK-UNSAFE" },
    { "E-ASM-NOUNSAFE",        "R-MARK-UNSAFE" },
    { "E-FFI-NOUNSAFE",        "R-MARK-UNSAFE" },
    { "E-UNSAFE-UNUSED",       "R-DROP-UNSAFE" },
    { "W-EFFECT-OVER",         "R-DROP-EFFECT" },
    { "E-EFFECT-REDUNDANT",    "R-DROP-EFFECT" },
    { "E-EFFECT-DUP",          "R-DROP-EFFECT" },
    { "E-TRAIT-EFFECT",        "R-DROP-EFFECT" },
    { "E-EFFECT-NONE-MIX",     "R-DROP-EFFECT-NONE" },
    { "E-EFFECT-UNDEF",        "R-USE-KNOWN-EFFECT" },
    { "E-EFFECT-CALC",         "R-CALC-TO-PROC" },
    { "E-EFFECT-PURITY",       "R-HANDLER-TO-PROC" },
    { "E-TIER-EFFECT",         "R-RAISE-TIER" },

    // 이름
    { "E-NAME-ASCII",          "R-RENAME-ASCII" },
    { "E-NAME-BUILTIN",        "R-RENAME" },
    { "E-NAME-DOTTED",         "R-RENAME" },
    { "E-NAME-DUP",            "R-RENAME" },
    { "E-NAME-KEYWORD",        "R-RENAME" },   // 낱말을 이름으로 — 답은 하나다: 다른 이름을 쓴다
    { "E-NAME-SHADOW",         "R-RENAME" },
    { "E-NAME-VENDOR-RESERVED","R-RENAME" },
    { "E-NAME-WILDCARD",       "R-RENAME" },
    { "E-NAME-COLLISION",      "R-RENAME-IMPORT" },
    { "E-NAME-QUALIFIER",      "R-QUALIFY-WITH-TYPE" },
    { "E-METHOD-UNDEF",        "R-SHORTEN-NAME" },

    // 없는 것을 선언하라
    { "E-TYPE-UNDEF",          "R-DECLARE-TYPE" },
    { "E-FN-UNDEF",            "R-DECLARE-OP" },
    { "E-CONTRACT-UNDEF",      "R-DECLARE-OP" },
    { "E-TRAIT-UNDEF",         "R-DECLARE-TRAIT" },
    // ★★ expr 섬(RFC-0091) — 둘 다 **수리가 코드로 정해진다**: 문맥을 볼 필요가 없다.
    //   `a lt b lt c` 는 언제나 `(a lt b) and (b lt c)` 로 갈라 쓰는 것이 답이고,
    //   `len d ge 4` 는 언제나 op 호출을 괄호로 묶는 것이 답이다.
    // ★★ bool (RFC-0092) — 둘 다 수리가 **코드로 정해진다**: 수를 조건/논리에 쓴 것이므로
    //   무엇을 견주는지 적으면 된다(`ne x 0`). 문맥을 볼 필요가 없다.
    // ★ 재귀 struct (RFC-0093) — 수리가 코드로 정해진다: 포인터가 없으니 **색인**을 쓴다.
    { "E-STRUCT-CYCLE",        "R-USE-INDEX" },
    // ★ 간접 순환도 지금의 수리는 같다(색인으로 짓는다). **이유가 다를 뿐**이다 —
    //   레이아웃이 아니라 수명 정책이라서, 언젠가 storage-ref 가 오면 이 수리가 바뀐다.
    //   (RFC-0104 §8-1 · §8-2. 그때 이 줄을 고치는 것이 신호다.)
    { "E-STRUCT-REF-UNMANAGED", "R-USE-INDEX" },
    { "E-TYPE-COND",           "R-USE-COMPARISON" },
    { "E-TYPE-LOGICAL",        "R-USE-COMPARISON" },
    { "E-EXPR-CHAIN",          "R-SPLIT-COMPARISON" },
    { "E-EXPR-APP",            "R-PARENTHESISE-CALL" },
    // ★ 섬에는 단항이 없다 — 고치는 법은 **접두 op 을 쓰는 것**이다(`neg`·`not`·`bit_not`).
    { "E-EXPR-UNARY",          "R-USE-PREFIX-OP" },
    { "E-IR-UNDEF",            "R-DECLARE-HANDLER" },
    { "E-REGION-UNDEF",        "R-ADD-REGION-PARAM" },
    { "E-OPT-DEPENDS",         "R-DECLARE-OPTION" },
    { "E-CONFIG-UNDEF",        "R-NAME-OPTION" },
    { "E-ERR-UNDEF",           "R-DECLARE-ERROR" },
    { "E-ERR-UNDECLARED",      "R-DECLARE-ERROR" },
    { "E-ENUM-NOVARIANT",      "R-USE-DECLARED-VARIANT" },
    { "E-MATCH-UNDEF",         "R-USE-DECLARED-VARIANT" },
    { "E-ENUM-NOFIELD",        "R-USE-DECLARED-FIELD" },
    { "E-TYPE-FIELD",          "R-USE-DECLARED-FIELD" },
    { "E-VISIBILITY",          "R-EXPORT-OP" },
    { "E-CFG-UNKNOWN-PROP",    "R-USE-KNOWN-PROP" },
    { "E-REQ-UNDEF",           "R-DECLARE-CONTRACT-NAME" },
    { "E-ENS-UNDEF",           "R-DECLARE-CONTRACT-NAME" },

    // 타입 — 변환·일치·가변성
    { "E-TYPE-MIX",            "R-CONVERT-EXPLICIT" },
    { "E-TYPE-NOMINAL",        "R-CONVERT-NEWTYPE" },
    // ★ 같은 틀의 **다른 인스턴스**(X-0009). 수리는 문맥 없이도 정해진다: 선언을 **값과 같은
    //   인스턴스**로 적는 것이다(변환이 아니다 — 레이아웃이 달라 변환할 것이 없다).
    { "E-TYPE-INSTANCE",       "R-MATCH-TYPE" },
    // ★ 브랜드 재사용(RFC-0104 §8-2). 수리는 문맥 없이 정해진다: **새 `newtype` 을 하나 더
    //   선언해 이 자리에 쓰는 것**이다. 브랜드는 자료를 안 나르므로 하나 더 두는 값이 0 이다.
    { "E-BRAND-REUSED",        "R-NEW-BRAND" },
    // ★ 서로 다른 명명 타입(X-0010). 수리는 같다 — **선언을 값의 타입으로** 적는 것이다.
    //   (표현이 같아도 뜻이 다르므로 "변환" 은 수리가 아니다: 무엇으로 변환할지는 사람이 정한다.)
    { "E-TYPE-STRUCT",         "R-MATCH-TYPE" },
    // ★ `option` ↔ `result`. 수리는 **받은 것을 풀거나 선언을 맞추는 것**이고, 둘 다
    //   "타입을 맞춰라" 의 실물이다 — 변환할 값이 따로 있는 것이 아니다.
    { "E-TYPE-WRAP",           "R-MATCH-TYPE" },
    { "E-TYPE-SIGN",           "R-WIDEN-SIGNED" },
    { "E-TYPE-WIDTH",          "R-NARROW-EXPLICIT" },
    { "E-LIT-RANGE",           "R-SMALLER-LITERAL" },
    { "E-TYPE-KIND",           "R-USE-SELECT" },
    { "E-TYPE-ARG",            "R-MATCH-TYPE" },
    { "E-TYPE-RETURN",         "R-MATCH-TYPE" },
    { "E-TYPE-SET",            "R-MATCH-TYPE" },
    { "E-TYPE-VAR",            "R-MATCH-TYPE" },
    { "E-TYPE-MUT",            "R-DECLARE-MUT" },
    { "E-TYPE-ARGMUT",         "R-DECLARE-MUT" },
    { "E-TYPE-RETMUT",         "R-DECLARE-MUT" },
    { "E-TYPE-REF",            "R-DECLARE-MUTREF" },
    { "E-IMMUTABLE",           "R-LET-TO-VAR" },
    { "E-TYPE-TRY",            "R-USE-RESULT" },
    { "E-TYPE-MASK",           "R-USE-COMPARE" },
    { "E-TYPE-LANES",          "R-MATCH-LANES" },
    { "E-TYPE-ALIGN",          "R-USE-POW2-ALIGN" },
    // ★ 타깃 정렬이 공개 표면에 나왔다(RFC-0104 §8-7). 수리는 문맥 없이 정해진다:
    //   **경계 타입에는 리터럴 정렬**을 적는다(타깃 정렬이 필요하면 그 타입을 내부에 둔다).
    { "E-ABI-TARGET-ALIGN",    "R-USE-LITERAL-ALIGN" },
    // ★ 상수만으로 이미 거짓인 계약(RFC-0104 §8-3). 수리는 문맥 없이 정해진다:
    //   **그 자리의 상수를 계약이 허용하는 값으로 바꾼다**(bit 예산이면 합을 word 안으로).
    { "E-CONTRACT-IMPOSSIBLE",  "R-FIX-CONST-ARG" },
    { "E-TYPE-CYCLE",          "R-BREAK-ALIAS-CYCLE" },
    { "E-TYPE-DECL",           "R-USE-STRUCT-FORM" },
    { "E-TYPE-ITER",           "R-ITER-SLICE" },
    { "E-TYPE-LAYOUT",         "R-USE-KNOWN-LAYOUT" },
    { "E-TYPE-BITCAST",        "R-USE-PLAIN-TYPE" },
    { "E-TYPE-LIMIT",          "R-SPLIT-STRUCT" },
    { "E-TYPE-RANGE",          "R-FIX-RANGE" },
    { "E-BITOP-TYPE",          "R-USE-INTEGER" },
    { "E-SHIFT-RANGE",         "R-BOUND-SHIFT" },
    { "E-VEC-QUAL",            "R-DROP-QUALIFIER" },
    { "E-BOUND-UNSAT",         "R-SATISFY-BOUND" },
    { "E-ATOMIC-ORDER",        "R-USE-KNOWN-ORDER" },
    { "E-ACCESS-MODE",         "R-DECLARE-WRITE-ACCESS" },

    // 소유 · 차용
    { "E-OWN-MOVED",           "R-COPY-OR-REORDER" },
    { "E-OWN-JOIN",            "R-CONSUME-BOTH-PATHS" },
    { "E-OWN-PARTIAL",         "R-MOVE-WHOLE" },
    // ★ 낱말이 없다 — 수리는 **그 낱말을 적는 것**이다(값을 바꾸는 일이 아니다).
    { "E-OWN-BARE",            "R-DECLARE-OWNED" },
    { "E-OWN-INCOMPLETE",      "R-ADD-DROP" },
    { "E-BORROW-ESCAPE",       "R-KEEP-IN-BORROW" },
    { "E-BORROW-EXCL",         "R-END-BORROW-FIRST" },
    { "E-EXCL",                "R-END-BORROW-FIRST" },
    { "E-EXCL-MOVED",          "R-END-BORROW-FIRST" },
    { "E-ESCAPE",              "R-RETURN-VALUE" },
    // ★ `mut ref slice` 가 **유일하게** 더 주는 것은 호출자의 서술자를 갈아끼우는 일이고,
    //   그 뜻을 정직하게 말하는 길은 **반환값** 하나뿐이다(E-ESCAPE 와 같은 수리).
    //   ☞ "원소만 쓰려던 것이면 `mut slice`" 는 애초에 거절되지 않는 **다른 코드**이므로
    //     수리가 갈리지 않는다 — 지어낸 수리가 아니라 진단이 이미 말하던 것이다.
    { "E-MREF-SLICE",          "R-RETURN-VALUE" },
    { "E-REGION-ESCAPE",       "R-COPY-OUT-OF-REGION" },

    // match · enum
    { "E-MATCH-INEXHAUSTIVE",  "R-ADD-MISSING-CASE" },
    { "E-MATCH-REDUNDANT",     "R-DROP-CASE" },
    { "E-MATCH-ARITY",         "R-BIND-ONE-PAYLOAD" },
    { "E-MATCH-ORBIND",        "R-SAME-ENUM-ORBIND" },
    { "E-ENUM-UNCHECKED",      "R-GUARD-VARIANT" },
    { "E-ENUM-VARIANT",        "R-GUARD-VARIANT" },
    { "E-ENUM-ARITY",          "R-MATCH-PAYLOAD-ARITY" },
    { "E-ENUM-PAYLOAD",        "R-SINGLE-WORD-PAYLOAD" },
    { "E-ENUM-INFINITE",       "R-BREAK-RECURSION" },

    // 계약
    { "E-CONTRACT-DEAD",       "R-DROP-ERROR" },
    { "E-CONTRACT-MODE",       "R-USE-KNOWN-MODE" },
    { "E-REQ-UNSUP",           "R-SIMPLIFY-CONTRACT" },
    { "E-ENS-UNSUP",           "R-SIMPLIFY-CONTRACT" },
    { "E-GUARD-FALLTHROUGH",   "R-LEAVE-IN-ELSE" },
    { "W-UNBOUND",             "R-ADD-SATISFIES" },

    // 쪼개기(parallel)
    { "E-PAR-WRITE",           "R-OWN-ELEMENT-ONLY" },
    { "E-PAR-READ",            "R-OWN-ELEMENT-ONLY" },
    { "E-PAR-CARRY",           "R-OWN-ELEMENT-ONLY" },
    { "E-PAR-ASSOC",           "R-USE-ASSOCIATIVE" },
    { "E-PAR-FLOAT",           "R-DROP-PARALLEL" },
    { "E-PAR-NOLOOP",          "R-ADD-INDEX-LOOP" },

    // 동시성
    { "E-CONC-ALONE",          "R-ADD-PEER-TASK" },
    { "E-CONC-DEADLOCK",       "R-ADD-SENDER" },
    { "E-SPAWN-SCOPE",         "R-WRAP-TASK-GROUP" },
    { "E-CHAN-UNBOUNDED",      "R-BOUND-CHANNEL" },
    { "E-LOCK-NOTYET",         "R-USE-ACTOR" },
    { "E-ACTOR-FAILURE",       "R-DROP-DUP-CLAUSE" },
    { "E-ACTOR-MAILBOX",       "R-DROP-DUP-CLAUSE" },

    // 인터럽트
    { "E-ISR-CALLED",          "R-DROP-CALL" },
    { "E-ISR-PARAMS",          "R-DROP-PARAMS" },
    { "E-ISR-OUTPUT",          "R-VOID-OUTPUT" },

    // 어셈블리 · 명령어 집합
    { "E-ASM-BIND",            "R-BIND-OPERAND" },
    { "E-ASM-UNBOUND",         "R-BIND-OPERAND" },
    { "E-ASM-UNUSED",          "R-DROP-OPERAND" },
    { "E-ASM-OPERAND",         "R-USE-OPERAND-FORM" },
    { "E-ASM-BODY",            "R-ONE-TEXTLIT" },
    { "E-ASM-TARGET",          "R-NAME-ISET" },
    { "E-ASM-TARGET-UNKNOWN",  "R-USE-KNOWN-TARGET" },
    { "E-ASM-OPTLIE",          "R-DROP-OPTION-PURE" },
    { "E-TARGET-ISET",         "R-USE-KNOWN-ISET" },
    { "E-TARGET-LEAF",         "R-CHANGE-TARGET" },   /* POSIX 잎 ↔ POSIX 아닌 호스트(win64) — 타깃을 바꾸거나 그 잎을 안 쓴다 */
    { "E-TARGET-INTRIN",       "R-CHANGE-TARGET" },

    // C 경계
    { "E-FFI-BODY",            "R-DROP-BODY" },
    { "E-FFI-TYPE",            "R-USE-ABI-TYPE" },
    { "E-FN-NOTEXPORT",        "R-EXPORT-EXTERN" },
    { "E-FN-VALUE",            "R-NAME-OP" },

    // 빌드 · 프로파일 · 매니페스트
    { "E-BUILD-MODE",          "R-USE-KNOWN-VALUE" },
    { "E-BUILD-TIER",          "R-USE-KNOWN-VALUE" },
    { "E-PROFILE-UNKNOWN",     "R-USE-KNOWN-VALUE" },
    { "E-PROFILE-LEVEL",       "R-RAISE-PROFILE" },
    { "E-CONFIG-DEPENDS",      "R-ENABLE-DEPENDENCY" },
    { "E-CONFIG-TYPE",         "R-USE-DECLARED-OPTION-TYPE" },
    { "E-OPT-TYPE",            "R-USE-DECLARED-OPTION-TYPE" },
    { "E-OPT-UNUSED",          "R-DROP-OPTION" },
    { "E-PKG-DUP",             "R-DROP-DUP-KEY" },
    { "E-PKG-FORM",            "R-USE-PACKAGE-FORM" },
    { "E-PKG-KEY",             "R-USE-KNOWN-KEY" },
    { "E-PKG-NONAME",          "R-ADD-PACKAGE-NAME" },
    { "E-PKG-NOVERSION",       "R-ADD-PACKAGE-VERSION" },
    { "E-PKG-VERSION",         "R-USE-SEMVER" },

    // 타깃이 못 하는 것 — 프로그램을 바꾸거나 타깃을 바꾼다
    { "E-HEAP-NOHOST",         "R-USE-FIXED-WINDOW" },
    { "E-HEAP-NOCAP",          "R-ADD-HEAP-CAP" },
    { "E-ATOMIC-NOCAP",        "R-ADD-ATOMIC-CAP" },
    { "E-FLOAT-NOFLOAT",       "R-USE-FIXED-POINT" },
    { "E-CAP-NOHOST",          "R-DROP-OS-CAP" },
    { "E-MMIO-NOHOST",         "R-CHANGE-TARGET" },
    { "E-RESERVE-NOHOST",      "R-CHANGE-TARGET" },
    { "E-MMIO-PERM",           "R-DROP-WRITE" },
    { "E-MMIO-NOBASE",         "R-ADD-BASE" },

    // 문법 · 어휘 — 닫아라 / 고쳐라
    { "E-BLOCK-UNCLOSED",      "R-ADD-END" },
    { "E-GROUP-UNCLOSED",      "R-CLOSE-PAREN" },
    { "E-PAREN-UNCLOSED",      "R-CLOSE-PAREN" },
    { "E-PAREN-ESCAPE",        "R-CLOSE-PAREN" },
    { "E-PAREN-STRAY",         "R-DROP-PAREN" },
    // ★ X-0052 ⓐ — `end`·`do` 뒤의 점은 닫을 것이 없다. 수리는 하나: 지운다.
    { "E-DOT-STRAY",           "R-DROP-DOT" },
    // ★ 2026-09-25 — `end` 는 자기 `do` 만 닫는다: 닫히지 않은 문장은 자기 점을 받고, 머리 없는 블록은 머리를 받는다.
    { "E-DOT-MISSING",         "R-ADD-DOT" },
    { "E-BLOCK-NOHEAD",        "R-ADD-HEAD" },
    { "E-STR-UNTERM",          "R-CLOSE-STRING" },
    { "E-CHAR-UNTERM",         "R-CLOSE-STRING" },
    // ★ 구분자가 자릿수 사이가 아닌 자리에 있다 — 수리는 **하나뿐이다: 지운다.**
    //   («옮긴다» 가 아니다: 어디로 옮길지는 저자의 뜻이고, 지우면 언제나 같은 값이 남는다.)
    { "E-NUM-SEP",             "R-DROP-SEPARATOR" },
    // ★ 중첩이 처리기가 세는 깊이를 넘었다 — 수리는 **이름을 붙여 쪼개는 것**이다.
    //   («깊이를 줄여라» 가 아니라 «단계에 이름을 줘라»: 그래야 사람이 읽을 수도 있다.)
    { "E-NEST-DEPTH",          "R-SPLIT-INTO-STEPS" },
    // ★ 넓히기 표(§6.2.5)를 어긴 셋 — 수리는 **다른 낱말을 쓰는 것**이고 어느 낱말인지가
    //   위반의 종류로 정해진다. 갈래를 넘거나 부호를 바꾸려면 `cast`, 좁히려면 `narrow`.
    { "E-WIDEN-KIND",          "R-USE-CAST" },
    { "E-WIDEN-SIGN",          "R-USE-CAST" },
    { "E-WIDEN-NARROW",        "R-USE-NARROW" },
    { "E-STR-NEWLINE",         "R-CLOSE-STRING" },
    { "E-STR-ESCAPE",          "R-FIX-ESCAPE" },
    { "E-TEXTLIT-TERM",        "R-CLOSE-TEXTLIT" },
    { "E-TEXTLIT-UNTERM",      "R-CLOSE-TEXTLIT" },
    { "E-TEXTLIT-TAIL",        "R-CLOSE-TEXTLIT" },
    { "E-NOTE-UNTERM",         "R-CLOSE-NOTE" },
    { "E-DOT-DOUBLE",          "R-USE-METHOD-FORM" },
    { "E-FIELD-GLUED",         "R-USE-FIELD-FORM" },
    { "E-LEX-UTF8",            "R-FIX-UTF8" },
    { "E-TOPLEVEL",            "R-USE-DECL-HEAD" },
    { "E-STMT-NODO",           "R-ADD-DO-END" },
    { "E-CTRL-NODO",           "R-ADD-DO-END" },   // RFC-0113 R4 — 한 폼 몸을 `do … end` 로 감싼다
    { "E-CTRL-NODOT",          "R-ADD-DOT" },      // RFC-0113 R4 — `do` 앞에 점(`--fmt` 이 찍는다)
    { "E-NOT-IS-SOME",         "R-USE-IS-NONE" },  // RFC-0113 O5 — `not is_some x` → `is_none x`
    { "E-EFFECT-MISSING",      "R-INSERT-INFERRED-EFFECTS" },   // RFC-0113 R1 — `--doc` 의 inferred 를 절로
    { "E-BITSET-ADD",          "R-ADD-TO-INSERT" },   // RFC-0113 R5 — `add s n` → `bitset_insert s n`
    { "W-PROC-PURE",           "R-PROC-TO-FN" },      // RFC-0113 O3 — `proc … effects none .` → `fn …`
    { "W-VAR-NEVER-SET",       "R-VAR-TO-LET" },      // RFC-0113 O4 — 안 바뀌는 `var` → `let`
    { "E-CLOSER-EXTRA",        "R-DROP-DOT" },        // RFC-0113 R6 — 아무것도 안 닫는 점을 지운다
    { "E-NAME-CLAUSE",         "R-RENAME" },          // RFC-0113 R6 — 절 낱말을 입력 이름으로
    { "E-HEAD-NOT-AN-OP",      "R-DROP-DOT-HEAD" },
    { "E-METHOD-RECV",         "R-ADD-RECEIVER" },
    { "E-FIELD-FORM",          "R-USE-FIELD-FORM" },
    { "W-COL0",                "R-MOVE-TO-COL0" },

    // 파이프라인
    { "E-PIPE-NO-TERMINAL",    "R-MOVE-BEFORE-TERMINAL" },
    { "E-PIPE-STAGE",          "R-USE-COMPTIME-COUNT" },
    { "E-FOLD-OP",             "R-NAME-OP" },
    { "E-PIPE-WITH",           "R-DROP-OPERANDS" },      // RFC-0121 — op 이름 뒤에는 `with <식>` 하나만
    { "E-PIPE-CONTEXT-ARG",    "R-MATCH-TYPE" },         // 문맥 자리의 매개변수 수·타입을 맞춘다
    { "E-PIPE-CONTEXT-TYPE",   "R-MATCH-TYPE" },         // 참조 없는 값(수·bool·열거의 구조체)으로 문맥을 적는다
    { "E-LOCAL-CAPTURE",       "R-PASS-AS-INPUT" },      // RFC-0121 — 바깥 값은 매개변수로 건넨다
    { "E-LOCAL-PLACE",         "R-MOVE-TO-TOP" },        // 본문 바로 아래로, 아니면 최상위로
    { "E-LOCAL-EXPORT",        "R-MOVE-TO-TOP" },        // 밖에서 쓰려면 최상위 선언
    { "E-MAP-SINK",            "R-USE-MUT-SINK" },
    { "E-MAP-ELEM",            "R-USE-SCALAR-ELEM" },

    // comptime
    { "E-COMPTIME-ARG",        "R-USE-LITERAL" },
    { "E-COMPTIME-NONCONST",   "R-SIMPLIFY-COMPTIME" },
    { "E-CONST-NOTCOMPTIME",   "R-SIMPLIFY-COMPTIME" },

    // 트레이트
    { "E-TRAIT-MISSING",       "R-IMPLEMENT-OP" },
    { "E-TRAIT-RECV",          "R-SATISFIES-ON-TYPE" },
    { "E-TRAIT-SIG",           "R-MATCH-TRAIT-SIG" },

    // 진입점 · 한계
    { "E-ENTRY-OUTPUT",        "R-RETURN-EXIT-STATUS" },
    { "E-ENTRY-PARAMS",        "R-CAP-INPUTS-ONLY" },
    { "E-IR-ARITY",            "R-FEWER-PARAMS" },
    { "E-IR-LOCALS",           "R-SPLIT-OP" },
    { "E-PARSE-LIMIT",         "R-SPLIT-OP" },     // ★ 2026-09-27 — 노드 아레나가 찼다: 단위를 나눈다
    { "E-EXPECT-PLACE",        "R-USE-CONTRACT" }, // ★ X-0072 — 시험 밖 expect: requires/ensures 로(또는 panic)
    { "E-LET-NOTYPE",          "R-WRITE-TYPE" },   // ★ RFC-0132 T1 — `let x be u64 300 .`
    { "E-LET-OLDFORM",         "R-MOVE-TYPE" },    // ★ RFC-0132 T1 — 타입을 `be` 뒤로
    { "E-IR-UNSUP",            "R-SHORTEN-LITERAL" },
    { "E-IR-EXTRA",            "R-DROP-OPERANDS" },
    { "E-VOCAB-REMOVED",       "R-USE-REPLACEMENT" },
    { "E-DEF-HEAD",            "R-USE-REPLACEMENT" },   // `def` 는 타입만 짓는다 — 낱말을 빼거나 struct·enum·type·newtype 을 적는다
    { "W-USE-EXTERNAL",        "R-INLINE-MODULE" },
    { "W-TEST-NOT-RUN",        "R-RUN-TESTS" },

    // VM 트랩 중 **수리가 코드로 정해지는 것** — 나머지는 아래 NOREPAIR 다
    { "E-VM-BOXPOOL",          "R-REDUCE-LIVE-VALUES" },
    { "E-VM-RECPOOL",          "R-REDUCE-LIVE-VALUES" },
    { "E-VM-STKPOOL",          "R-REDUCE-LIVE-VALUES" },
    { "E-VM-BSETPOOL",         "R-REDUCE-LIVE-VALUES" },
    { "E-VM-CHAN",             "R-REUSE-HANDLE" },
    { "E-VM-MBOX",             "R-REUSE-HANDLE" },
    { "E-VM-FILEPOOL",         "R-CLOSE-HANDLE" },
    { "E-VM-SOCKPOOL",         "R-CLOSE-HANDLE" },
    { "E-VM-DIV0",             "R-GUARD-ZERO" },
    { "E-VM-SHIFT",            "R-BOUND-SHIFT" },
    { "E-VM-BOUNDS",           "R-GUARD-INDEX" },
    { "E-VM-NONE",             "R-MATCH-BEFORE-UNWRAP" },
    { "E-VM-ERR",              "R-MATCH-BEFORE-UNWRAP" },
    { "E-VM-READONLY",         "R-DECLARE-MUTREF" },
    { "E-VM-DEPTH",            "R-REDUCE-RECURSION" },
    { "E-VM-STACK-BUDGET",     "R-REDUCE-RECURSION" },
    { "E-VM-OOM",              "R-REDUCE-RECURSION" },
    { "E-VM-STACK",            "R-REDUCE-RECURSION" },
    { "E-VM-ASM",              "R-RUN-NATIVE" },
    { "E-VM-EXTERN",           "R-RUN-NATIVE" },
    { "E-VM-MMIO",             "R-RUN-NATIVE" },
    { "E-VM-CSTR",             "R-RUN-NATIVE" },
    { "E-VM-RESERVE",          "R-RUN-NATIVE" },
    { "E-VM-ARITY",            "R-MATCH-ARITY" },
    { "E-VM-UNDEF",            "R-DECLARE-OP" },
    { "E-VM-FMODE",            "R-USE-KNOWN-VALUE" },
    { "E-VM-FWHENCE",          "R-USE-KNOWN-VALUE" },
    { "E-VM-FD",               "R-USE-KNOWN-VALUE" },
    { "E-VM-CHAN-EMPTY",       "R-ADD-SENDER" },
    { "E-VM-CHAN-FULL",        "R-ADD-RECEIVER" },
    { "E-VM-DEADLOCK",         "R-ADD-SENDER" },
    { "E-VM-MAILBOX-FULL",     "R-BOUND-CHANNEL" },
    { "E-VM-AWAIT",            "R-USE-JOB-HANDLE" },
    { "E-VM-FHANDLE",          "R-USE-OPEN-HANDLE" },
    { "E-VM-SHANDLE",          "R-USE-OPEN-HANDLE" },
    { "E-VM-REACTOR",          "R-USE-OPEN-HANDLE" },
    { "E-VM-UNSUP",            "R-STAY-IN-CORE" },
    { "E-VM-EXCL",             "R-END-BORROW-FIRST" },
    { "E-VM-DANGLING",         "R-RETURN-VALUE" },
    { "E-VM-TYPE",             "R-USE-RESULT" },
    { "E-VM-FIELD",            "R-USE-DECLARED-FIELD" },
    { "E-VM-VIEW",             "R-MATCH-ELEM-SIZE" },
    { "E-VM-ALIGN",            "R-ALIGN-BASE" },
    { "E-VM-CONTRACT",         "R-FIX-ARGUMENT" },
    { "E-VM-OVERFLOW",         "R-WIDEN-SIGNED" },
    { "E-VM-SCHED-LOOP",       "R-STOP-RESENDING" },
    // ★ 2026-09-16 ~ 09-27 에 생긴 코드 — 2026-09-28 t4 게이트가 «분류 안 됨» 41 개로 잡았다(그 사이 t4 가 이 검사까지 못 갔다).
    //   고침이 하나로 정해지는 것만 여기에 둔다. 나머지는 NOREPAIR 에 이유와 함께.
    { "E-ABSORB-NOCONTRACT",   "R-ADD-REQUIRES" },       // 흡수 op 의 문에 `requires` 하나 이상
    { "E-ABSORB-NOREF",        "R-ADD-REFERENCE" },      // `reference <op> .` — 순수 Lowent 기준 구현
    { "E-ABSORB-NOWHY",        "R-ADD-WHY" },            // `why "…" .` — 비지 않은 이유
    { "E-ACTOR-FIELD",         "R-READ-BY-MESSAGE" },    // 상태를 돌려주는 op 을 actor 에 두고 메시지로 읽는다
    { "E-ACTOR-UNINIT",        "R-SEND-INIT-FIRST" },    // 그 칸을 `set` 하는 메시지를 먼저 보낸다
    { "E-BUILTIN-BARE",        "R-USE-CALL-BUILTIN" },   // `call_builtin <이름> …`
    { "E-BUILTIN-NAME",        "R-USE-KNOWN-BUILTIN" },  // 닫힌 집합의 이름을 첫 피연산자로
    { "E-CAP-LOCAL",           "R-PASS-CAP-PARAM" },     // 지역 이름 대신 받은 매개변수를 잎에 건넨다
    { "E-FFI-LINK",            "R-ADD-LINK" },           // `link "<C 이름>" .`
    { "E-FOLD-ORDER",          "R-MATCH-TYPE" },         // 첫 입력을 출력과 같은 타입으로
    { "E-INPLACE-FORM",        "R-USE-INPLACE-FORM" },   // `inplace <쓰는 mut 입력> <읽는 입력> .`
    { "E-INVALIDATES-FORM",    "R-USE-INVALIDATES-FORM" }, // `invalidates <입력 하나> .`
    { "E-MMIO-PLAIN",          "R-USE-VOLATILE" },       // `read_volatile` / `write_volatile`
    { "E-MONO-NOTYPE",         "R-ADD-TYPE-ARG" },       // 부르는 자리에 타입을 먼저 적는다
    { "E-NAME-SCOPE",          "R-DECLARE-BEFORE-BLOCK" }, // 블록 앞에서 선언하고 안에서 `set`
    { "E-PAR-IDENTITY",        "R-START-FROM-IDENTITY" },  // 항등원에서 시작하고 치우침은 루프 뒤에 한 번
    { "E-PIPE-PRED",           "R-USE-COMPARISON" },     // 물음을 비교로 적는다(`ne x 0`)
    { "E-STMT-ELSE",           "R-MOVE-ELSE-INSIDE" },   // `if c . do … else … end`
    { "E-TYPE-REFVAL",         "R-DEREF" },              // `deref <이름>` 으로 읽는다
    { "E-ARRAY-SET",           "R-USE-INDEX" },          // 배열은 다시 묶지 않는다 — `set (index buf i) v`
    { "E-USE-ALIASED",         "R-USE-ALIAS-NAME" },     // `as` 로 붙인 이름으로 적는다
    { "E-VEC-SPLAT",           "R-BIND-OPERAND" },       // 레인 수를 적은 바인딩으로 먼저 묶는다
    { "E-VIEW-INVALIDATED",    "R-RETAKE-VIEW" },        // 그 호출 뒤에 뷰를 다시 얻는다
};

// ── **수리가 없다** — 그리고 왜 없는지 (게이트가 이 목록도 읽는다) ──────────────
// ☞ 이 목록에 있는 것은 *"아직 안 적었다"* 가 아니라 *"코드만으로는 정할 수 없다"* 이다.
static const low_repair_row_t NOREPAIR[] = {
    { "E-VALUE-DISCARDED",  "길이 둘이다 — 값을 이름에 `set` 하려던 것인지, 문장을 지우려던 것인지는 저자의 뜻이다(RFC-0113 R5)" },
    // ★ RFC-0112 (WO-0211–0222) 에서 생긴 코드 — 고칠 길이 여럿이고 어느 쪽인지는 저자의 뜻이다
    { "E-ALLOC-NOROOT",     "어느 뿌리에서 깎을지는 저자의 뜻이다 — 권한 입력(`cap allocator`·`cap heap`)을 받을 수도, 영역 매개변수를 받을 수도, 영역 블록을 열어 그 안에서 생략형을 쓸 수도 있다" },
    { "E-ALLOC-NESTED",     "길이 둘이다 — 할당을 안쪽 영역의 이름으로 옮기거나, 안쪽 영역을 할당 뒤로 옮긴다. 어느 쪽이 뜻인지는 값의 수명이 정한다" },
    { "E-ALLOC-OUTLIVES",   "길이 둘이다 — 받는 actor 를 영역 안에서 띄우거나, 영역 밖에서 얻은 바이트를 건넨다. 수명의 뜻은 저자가 안다" },
    { "E-ALLOC-SHARED",     "길이 둘이다 — `reserve` 가 `atomic` 인 얼로케이터로 바꾸거나, 태스크마다 제 얼로케이터를 준다" },
    { "E-ALLOC-TASK",       "태스크 안의 할당을 어디로 옮길지는 구조의 선택이다 — 부르는 쪽에서 미리 깎아 건네거나, 태스크마다 제 영역을 준다" },
    { "E-ALLOC-NOSOURCE",   "어느 얼로케이터를 줄지는 저자의 뜻이다 — 입력으로 받거나, 바인딩으로 만들거나, 받는 쪽 서명을 바꾼다" },
    { "E-CLAUSE-ORDER",     "입력이 아닌 절은 `--fmt` 가 옮기지만 **입력끼리** 어긋나면 호출 자리의 인자 차례까지 바뀐다 — 코드만으로 한 가지 수리가 정해지지 않는다(진단 문구가 어느 쪽인지 말한다)" },
    { "E-IF-VALUE",         "갈래마다 값을 정하는 모양이 여럿이다 — 이름에 `set` 하거나, 갈래마다 `return` 하거나, 식을 op 로 뺀다" },
    { "E-USING-FORM",       "무엇이 모자랐는지가 자리마다 다르다(이름·타입·자리) — 진단 문구가 그것을 말한다" },
    { "E-ENUM-FIELD",       "남은 낱말이 타입이 빠진 칸인지, 갈래 뒤에 빠진 점인지는 저자의 뜻이다" },
    { "E-RESULT-NOZERO",    "길이 둘이다 — 결과의 이름을 빼고 값을 돌려주거나, 0 이 정해진 타입(수 · bool · option · 그런 칸만 가진 구조체)으로 바꾼다. 어느 쪽이 뜻인지는 저자가 안다" },
    { "E-RESULT-PLACE",     "결과를 어느 이름에 묶을지(새 `let` 인지, 있던 배열인지)와 블록 밖 `return` 을 이름 붙은 결과로 바꿀지는 저자의 뜻이다" },
    { "E-RESULT-NAMED",     "몸이 없는 op 의 결과에서 이름을 뺄지, 몸을 가진 op 으로 감쌀지는 저자의 뜻이다" },
    { "E-TYPE-ARRAY",       "길이가 둘 중 어디 있는지(차례가 틀렸나, 자리가 틀렸나)는 진단이 말하고, 고칠 모양(`slice` + 계약으로 적을지)은 저자가 고른다" },
    { "E-USING-UNRESOLVED", "**프로그램의 잘못이 아니다** — 나무를 세우지 않는 대조 방식(`--flat`)으로 검사해서 `using` 이 안 풀렸다. 나무 방식으로 검사하면 된다" },
    { "E-TYPE-CYCLE-LIMIT", "**프로그램의 잘못이 아니다** — 순환 탐지기가 걸을 수 있는 것보다 struct 그래프가 크다. 저자가 고칠 것은 없고(단위를 나누는 것은 수리가 아니라 회피다), 고칠 자리는 컴파일러다." },
    { "E-VM-CHAIN-LIMIT",  "**프로그램의 잘못이 아니다** — VM 활성 사슬이 되감기 등록 한도를 넘었다. 자르면 살아 있는 값이 회수될 수 있어 거절한다. 저자가 적을 수리가 없다." },
    { "E-MONO-FIXPOINT",   "**프로그램의 잘못이 아니다** — 단형화의 고정점 되풀이가 예산 안에 안 끝났다는 뜻이고, 그것은 컴파일러의 한계다. 저자가 고칠 것이 없으므로 수리도 없다. (2026-08-30: 여기 있던 고정 표 셋을 없애고 남긴 **말하는 상한** 하나다 — 자르지 않고 거절한다.)" },
    { "E-CHAR-WIDTH",      "**넓히느냐 좁히느냐가 저자의 뜻**이다 — `u8` 을 `U`(코드포인트)로 넓힐 수도, 문자 대신 **문자열**로 바꿀 수도 있다. 둘은 다른 타입이고 쓰는 자리가 정한다. 도구가 하나를 고르면 그것은 수리가 아니라 **추측을 값으로 바꾸는 일**이다" },
    { "E-CHAR-EMPTY",      "빈 리터럴이 무엇을 뜻하려 했는지는 코드가 모른다 — 문자를 넣으려던 것일 수도, 빈 **문자열**을 적으려던 것일 수도 있다" },
    { "E-CHAR-NEWLINE",    "줄이 끝나기 전에 닫으려던 것인지, 개행을 값으로 넣으려던 것인지(`'\\n'`) 자리마다 다르다" },
    { "E-STR-PREFIX",      "어느 접두사를 **뜻했는지**는 저자만 안다 — `u8`(바이트)·`u`(UTF-16)·`U`(코드포인트)는 서로 **다른 타입**을 내고, 접두사를 지우는 것 또한 뜻이 다르다. 코드가 하나를 고르면 그것은 수리가 아니라 **추측을 값으로 바꾸는 일**이다. ☞ 진단 문구가 닫힌 집합 셋을 그대로 나열한다 — 고를 사람은 저자다" },
    { "E-CHAR",            "**지어낸 수리였다**(2026-08-04) — 뜻밖의 문자는 지울 수도, 바꿀 수도, 이스케이프할 수도 있다. 코드가 정하지 못한다. ☞ 이 자리를 **기존 골든 증인이 지키고 있었고**(`repair id: absent when unknown`), 레지스트리를 처음 채운 날 그 증인이 나를 잡았다" },
    { "E-LET-NOVALUE",    "고칠 길이 **둘**이고 어느 쪽인지는 저자의 뜻이다 — 빠진 값을 적는 것과, `.5` 처럼 앞선 점 때문에 값이 사라진 것이라면 `0.5` 로 고쳐 적는 것. **어떤 값**인지는 코드가 모른다. 다만 진단이 두 길을 **둘 다 말해 준다** — 그것이 이 자리에서 도구가 할 수 있는 전부다" },
    { "E-RETURN-PARTIAL",  "고칠 길이 **둘**이고 어느 쪽인지는 저자의 뜻이다 — 빠진 길에 `return <값>` 을 적는 것과, 이 op 이 정말 값을 안 낸다면 `output void` 로 고쳐 적는 것. 게다가 앞쪽을 고른다 해도 **어떤 값**인지는 코드가 모른다. 도구가 하나를 고르면 그것은 수리가 아니라 **추측을 값으로 바꾸는 일**이다(`E-CHAR` 에서 이미 한 번 배웠다)" },
    { "E-VM-PANIC",        "프로그램이 **일부러** 멈춘 것이다 — 고칠 것이 있는지는 그 프로그램의 뜻에 달렸다" },
    { "E-FORM-UNEXPECTED", "무엇이 와야 했는지는 **자리마다 다르다** — 파서가 그 자리에서 아는 것이고 코드가 아니다" },
    { "E-TEST-FAIL",       "고칠 자리가 **프로그램일 수도 기대값일 수도** 있다 — 어느 쪽인지는 도구가 모른다" },
    { "E-VM-ANALYSIS",     "**컴파일러의 결함**이다(구간 분석이 불건전) — 사용자가 고칠 것이 없다" },
    { "E-VM-BUDGET",       "오라클 실행의 스텝 예산일 뿐 — 프로그램의 결함이 아니다" },
    { "E-VM-CAST",         "메시지가 자리마다 달라 코드만으로 수리가 정해지지 않는다" },
    { "E-IR-LIMIT",        "무엇이 한계를 넘었는지가 자리마다 다르다 — 진단 문구가 그것을 말한다" },
    // ★ 2026-09-07 — 고칠 길이 **둘**이고 어느 쪽인지는 저자의 뜻이다:
    //   ① 절을 진입 검사가 아는 모양으로 다시 적는다(그러면 강제된다)
    //   ② 그대로 두고 `guard` 로 **값을 답한다**(폭이 실행 시각에 정해지면 이쪽뿐이다)
    //   코드만으로는 어느 쪽인지 정해지지 않는다 — 그래서 수리를 지어내지 않는다.
    // ★ 값은 **한 문자열 리터럴**이어야 한다 — 표를 읽는 게이트(check-repair)가 이어붙인 것을
    //   행으로 못 읽는다(실측: 두 조각으로 적었더니 «분류되지 않은 코드» 로 걸렸다).
    { "W-CONTRACT-IGNORED", "고칠 길이 **둘**이고 어느 쪽인지는 저자의 뜻이다 — 진입 검사가 아는 모양으로 다시 적으면 강제되고, `guard` 로 값을 답하면 실행 시각에 정해지는 폭도 다룬다" },
    { "E-EFFECT",          "옛 총칭 코드 — 구체 코드(E-EFFECT-*)로 갈라져 있고 수리는 그쪽에 있다" },
    { "E-FIELD-MARK",      "표식의 종류마다 고칠 것이 달라 코드만으로 정해지지 않는다" },
    { "E-TYPE-LET",        "`let` 자리의 무엇이 어긋났는지가 자리마다 다르다" },
    { "E-UPTR-TYPE",       "생 포인터가 어떤 자리에서 어긋났는지에 달렸다 — 코드가 정하지 못한다" },
    { "E-MMIO-BASE",       "기저 주소의 무엇이 문제인지(없음·겹침·정렬)가 자리마다 다르다" },
    { "E-MMIO-BYVALUE",    "레지스터를 값으로 나른 자리마다 고칠 모양이 다르다" },
    { "E-MMIO-FIELD",      "필드 정의의 무엇이 어긋났는지가 자리마다 다르다" },
    { "W-EXPORT-NOSYM",    "심볼이 안 나오는 **이유가 여럿**이고(하강 안 됨·이름·반환 모양) 그 이유마다 수리가 다르다 — `plan.why` 가 그 자리에서 말한다" },
    { "W-CBE-SLOW",        "느린 이유가 op 마다 다르다 — `--why-slow` 가 그 자리에서 말한다" },
    { "W-PAR-OK",          "**결함이 아니다** — 쪼갤 수 있다는 알림이다" },
    { "N-MONO-SITE",       "**결함이 아니다** — 단형화가 어디서 일어났는지 알리는 주석이다" },
    { "W-NOT-YET",         "도구가 아직 안 하는 일이다 — 프로그램에 고칠 것이 없다" },
    { "W-RFC-PENDING",     "설계가 아직 RFC 단계다 — 프로그램에 고칠 것이 없다" },
    // ★ 2026-09-28 — 위 REPAIR 의 같은 묶음. 고칠 길이 여럿이거나 사람이 정할 일이다.
    { "E-ABSORB-IMPURE",    "길이 둘이다 — 효과가 있는 부분을 다른 op 으로 떼어 내거나, 흡수를 그만두고 `unsafe` 를 호출자에게 보인다" },
    { "E-ABSORB-PLACE",     "누가 보증할 수 있는지는 사람이 정한다 — pkg.low 의 `build absorb` 줄과 흡수 장부의 서명은 도구가 대신 적을 것이 아니다" },
    { "E-ABSORB-SCOPE",     "경우가 둘이다 — 모양이 틀렸으면 `absorbs machine <이름> .` 으로, 세상에 닿는 권한이면 흡수 대신 권한 입력으로 받는다" },
    { "E-ACTOR-STATE-REF",  "길이 둘이다 — 값을 복사해 상태에 두거나, actor 가 받아 수명 내내 가진 슬라이스를 둔다" },
    { "E-BORROW-FIELD",     "길이 둘이다 — 필드를 지역으로 복사해 그것을 빌리거나, 값 전체를 `mut` 로 건넨다" },
    { "E-COLLECT-FULL",     "길이 둘이다 — 자리를 키우거나 넣는 원소를 줄인다. 어느 쪽 길이가 뜻인지는 저자가 안다" },
    { "E-CONTRACT-UNSAT",   "두 전제 가운데 어느 것이 뜻한 것인지는 쓴 사람만 안다" },
    { "E-ERRORS-STATE",     "조건을 어떤 입력으로 다시 쓸지는 op 의 뜻에 달렸다 — 바뀌는 것은 몸 안의 guard 로 옮긴다" },
    { "E-EXCL-INPLACE",     "겹치지 않게 저장소를 나누는 방법은 호출자의 배치에 달렸다" },
    { "E-INPLACE-UNPROVEN", "길이 둘이다 — 몸을 증명되는 두 모양 가운데 하나로 바꾸거나, `inplace` 선언을 지운다" },
    { "E-LOOP-OUTSIDE",     "뜻이 둘이다 — 루프가 빠졌거나 `return` 을 뜻했다. 어느 쪽인지는 쓴 사람만 안다" },
    { "E-NUM-EMPTY",        "빠진 숫자가 무엇인지는 쓴 사람만 안다(0 이면 `0x0`)" },
    { "E-NUM-SUFFIX",       "원인이 셋이다 — 접미사, 8진, 밑 밖의 숫자. 경우마다 고침이 다르다" },
    { "E-TRY-NORESULT",     "길이 둘이다 — 출력을 `result` 로 바꾸거나, 실패를 그 자리에서 처리한다" },
    { "E-TYPE-COLLECT",     "길이 둘이다 — 앞에 좁히는 `map`(어떤 좁힘인지 골라야 한다)을 두거나, 받는 버퍼를 넓힌다" },
    { "W-CONFIG-DEPENDS",   "길이 셋이다 — 그 코드를 의존 옵션으로 감싸거나, 설정 줄을 지우거나, `depends` 를 지운다" },
    { "W-ERRORS-UNRAISED",  "뜻이 둘이다 — 쓸데없는 절이면 지우고, 몸이 그 실패를 내야 했다면 몸을 고친다" },
    { "W-EXPORT-HIDDEN",    "길이 둘이다 — 타입도 내보내거나, 밖에서 이름할 수 있는 타입으로 서명을 바꾼다" },
    { "W-RESULT-DISCARD",   "길이 셋이다 — 묶어서 살피거나, `try` 로 넘기거나, 버리는 이유를 코드에 적는다" },
    // ★ RFC-0132 T2b-1 (2026-09-29)
    { "E-LIT-COUNT",        "길이 셋이다 — 원소를 더하거나 빼거나, 모자란 칸이 0 이어도 되면 끝에 `_` 를 적는다. 어느 것이 뜻인지는 저자가 안다" },
    { "E-VM-COPY",          "`copy` 의 두 조각 길이를 같게 한다 — 받는 쪽이나 주는 쪽을 `subslice` 로 잘라 맞춘다" },
    { "E-COPY-LEN",         "두 나열의 길이를 같게 하거나, 받는 쪽을 `subslice` 로 잘라 길이를 맞춘다" },
    { "E-FOR-HEAD",         "셈 이름이나 머리가 읽은 이름을 몸에서 바꾸지 말고 다른 이름에 담는다. `for x mut buf` 이면 몸에서는 `x` 로만 칸을 만진다" },
    { "E-FOR-STEP",         "셈의 타입을 정수로 적고(`for i range u64 3 10 .`), `step` 은 0 이 아닌 수로 적는다" },
    { "E-BIND-ELSE",        "길이 둘이다 — 값이 option/result 가 아니면 `else` 를 빼고, 알맹이 타입이 다르면 `be` 뒤의 타입을 알맹이의 타입으로 고친다. 모르는 식이면 먼저 타입을 적어 묶는다" },
    { "E-LIT-USING",        "길이 둘이다 — 할당자에서 받는 나열이면 `using <할당자>` 와 타입 `option` 을 함께 적고, 아니면 둘 다 뺀다(틀 안 자리)" },
    { "E-LIT-IDENTITY",     "길이 둘이다 — 자리가 아니라 내용을 견주거나(원소를 차례로), 한쪽을 `var` 나열로 받아 제 자리를 갖게 한다. 무엇을 알고 싶었는지는 저자가 안다" },
    { "E-LIT-INDEX",        "길이 여럿이다 — 번호를 길이 안의 상수로 고치거나, 두 번 적은 칸 하나를 지우거나, `_ <값> .` 을 끝으로 옮긴다. 어느 칸에 무엇이 뜻인지는 저자가 안다" },
    { "E-LIT-ESCAPE",       "길이 둘이다 — 필요한 것을 블록보다 오래 사는 저장소로 복사하거나, 나열을 더 바깥 블록에서 선언한다. 어느 쪽인지는 값의 수명이 정한다" },
    { "E-FRAME-SIZE",       "길이 둘이다 — 큰 표는 상수 `let`(읽기 전용, 틀을 안 쓴다)로 두거나, 영역(region)에서 받는다" },
    { "E-FRAME-RECURSIVE",  "길이 둘이다 — 재귀 안의 나열은 할당기에서 받거나(`using … else`), op 을 재귀하지 않게 고친다" },
    { "E-STACK-BUDGET",     "길이 둘이다 — 큰 나열을 할당기에서 받거나, 부르는 길을 나눠 한 번에 스택에 살아 있는 나열을 줄인다" },
    { "E-LINK-NAME",        "길이 둘이다 — `link` 이름을 C 가 받을 수 있는 이름으로 바꾸거나, `link` 를 지워 기본 이름(`lw_<길이>…`)을 쓴다" },
    { "E-LIT-UNBUILT",      "도구가 아직 짓지 않은 모양이다(RFC-0132 T2b-3) — 프로그램에 고칠 것이 아니라 다른 모양으로 적거나 기다린다" },
};

const char *low_repair_for(const char *code) {
    if (!code) return 0;
    for (unsigned i = 0; i < sizeof REPAIR / sizeof REPAIR[0]; i++)
        if (strcmp(REPAIR[i].code, code) == 0) return REPAIR[i].repair;
    return 0;   // NOREPAIR 이거나 분류 밖 — 게이트가 후자를 잡는다
}
