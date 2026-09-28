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

/* X-0070 — 수 리터럴이 부동인가(16진은 `.`·`p`, 10진은 `.`·`e`). 한 곳에서만 판정한다. */
bool low_num_is_float(proven_u8str_view_t v);

#endif // LOW_TYPECHECK_H
