// low_typecheck.h — MVP semantic analysis (S4): static type checking.
//
// A kind-level checker over MVP `fn`/`proc` signatures (input/output clauses):
// infers expression types (bool / integer / float / slice / named) and flags clear
// mismatches at `var N be type e`, `return e`, `set N e`, and call arguments.
// Integer width/signedness: literal range vs declared width, implicit narrowing,
// and signed/unsigned mixing are rejected (E-TYPE-WIDTH / E-TYPE-SIGN); `cast` is
// the explicit escape hatch. Conservative — UNKNOWN/NAMED types never flag.
#ifndef LOW_TYPECHECK_H
#define LOW_TYPECHECK_H

#include "proven/array.h"
#include "low_cst.h"

typedef struct { proven_array_t diags; bool ok; } low_typecheck_result_t;

[[nodiscard]] low_typecheck_result_t low_typecheck(proven_allocator_t work, const low_parse_result_t *pr);

/* ★★★ RFC-0148 S4-0 — **검사층이 식 마디에 낸 답을 적어 둔다** (`LOWENT_LIST_TYPES=1` 일 때만). 하강층이 같은 마디에서 제 그림자의
 *   답과 맞댄다(`low_ir.c` 의 `ir_node`). 두 층을 맞대 보는 것이 없었다는 것이 RFC-0148 §3 의 셋째 뿌리다. 재는 도구일 뿐
 *   — 아무 판정도 바꾸지 않는다. k: 0 모름 · 1 정수 · 2 부동 · 3 bool · 4 슬라이스 · 5 감싸개 · 6 이름 · 7 그 밖. */
typedef struct {
    proven_u8 k, bits, nominal; bool sign, lit;
    proven_u8 wrap, pk, pbits, pnom; bool psign;        /* 감싸개의 종류와 알맹이 */
    proven_u8 ebits, enom; bool esign, eflt;            /* 슬라이스의 원소 */
} low_tcnote_t;
bool low_tc_note_of(const void *node, low_tcnote_t *out);
bool low_tc_notes_on(void);
bool low_tc_notes_live(void);   /* 이 단위의 검사가 답을 적었다 — 하강층이 권위로 읽는다 (S4-2) */
bool low_tc_list_types(void);
void low_tc_notes_close(void);  /* 하강이 단위를 끝냈다 — 답을 닫는다 */   /* `LOWENT_LIST_TYPES=1` — 두 층의 답을 한 줄로 낸다 */

/* X-0070 — 수 리터럴이 부동인가(16진은 `.`·`p`, 10진은 `.`·`e`). 한 곳에서만 판정한다. */
bool low_num_is_float(proven_u8str_view_t v);

#endif // LOW_TYPECHECK_H
