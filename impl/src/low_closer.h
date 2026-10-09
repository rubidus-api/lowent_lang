#ifndef LOW_CLOSER_H
#define LOW_CLOSER_H

#include "proven/types.h"
#include "proven/array.h"
#include "low_token.h"
#include "low_diag.h"

// RFC-0142 §5-2 — 새 표면(«이름은 열고, 점은 닫는다»)의 토큰 열을 파서가 읽는 꼴로 옮긴다. 선언을 읽지 않는다.
//   성공하면 `tokens` 를 새 열로 바꾸고 true. 실패하면 진단 하나를 `diags` 에 넣고 false(토큰 열은 그대로다).
[[nodiscard]] bool low_closer_lower(proven_allocator_t heap, proven_array_t *tokens, proven_array_t *diags);

#endif // LOW_CLOSER_H
