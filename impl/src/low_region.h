// low_region.h — MVP semantic analysis (S4): region / escape (ESC) + EXCL.
//
// Escape: a reference to a *local* (a `var` declared in the op body) must not escape
// the op — returning `ref`/`mut_ref`/`addr` of a local yields a dangling reference
// once the body's region is torn down at op exit (`return ref localvar` → E-ESCAPE).
// References derived from parameters are fine (their referents outlive the op).
// This is C's "return &local" bug, rejected statically.
//
// EXCL (readers-XOR-writer, SPEC-004 §4.4 / RFC-0005 §6.3): no borrow of a local may
// overlap a live `mut_ref` of the same local (E-EXCL) — bound borrows and transient
// call-argument borrows both count. Liveness is lexical (a bound borrow lives to the
// end of its enclosing block; no CFG shortening) — conservative by construction;
// dataflow-precise liveness is a future refinement.
#ifndef LOW_REGION_H
#define LOW_REGION_H

#include "proven/array.h"
#include "low_cst.h"

typedef struct { proven_array_t diags; bool ok; } low_region_result_t;
// ★ 차용 이벤트를 표준출력으로 낸다(`--emit-events`) — 모델의 입력을 **IR 에서** 뽑기 위해.
void low_region_set_emit_events(bool on);

[[nodiscard]] low_region_result_t low_region(proven_allocator_t work, const low_parse_result_t *pr);

#endif // LOW_REGION_H
