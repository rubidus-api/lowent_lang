// low_contract.h — MVP semantic analysis (S4): contract checks (requires / errors).
//
// * errors-closure: an op may only produce errors listed in its `errors` clause —
//   a closed, declared error set (checked exceptions without the boilerplate).
//   `return error X` / `err X` with X not declared → E-ERR-UNDECLARED.
// * requires-resolve: a `requires` condition may only reference in-scope names
//   (params, declared ops, known predicate/prelude ops) — a typo'd contract is
//   caught statically → E-REQ-UNDEF. (The runtime truth of requires is a debug
//   check per SPEC-MVP §6; here we verify the contract is well-formed.)
#ifndef LOW_CONTRACT_H
#define LOW_CONTRACT_H

#include "proven/array.h"
#include "low_cst.h"

typedef struct { proven_array_t diags; bool ok; } low_contract_result_t;

[[nodiscard]] low_contract_result_t low_contract(proven_allocator_t work, const low_parse_result_t *pr);

#endif // LOW_CONTRACT_H
