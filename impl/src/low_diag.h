// low_diag.h — diagnostics for the shared front-end.
// Errors are values (proven house style): collected into an array, not thrown.
// The registry mirrors SPEC-MINI §3.1 (E-DOT-* lexer/parser, E-HEAD-*, E-FORM-*).
#ifndef LOW_DIAG_H
#define LOW_DIAG_H

#include "proven/types.h"

typedef enum {
    LOW_SEV_ERROR = 0,
    LOW_SEV_WARNING,
    LOW_SEV_NOTE,
} low_severity_t;

typedef struct {
    low_severity_t sev;
    const char    *code;   // stable code, e.g. "E-DOT-DOUBLE" (static string)
    const char    *msg;    // human render (static string, or points into `detail`)
    proven_u32     line;   // 1-based
    proven_u32     col;    // 1-based
    // self-contained buffer for formatted runtime messages.
    // ★ 96 → 256 (2026-08-07): 이름을 **대는** 진단이 늘면서 96 바이트가 문장을 잘랐다.
    //   자른 진단은 자리를 반만 짚는다 — 그럴 바에는 버퍼가 큰 편이 싸다.
    char           detail[256];
    // ★ optional STABLE repair id, e.g. "R-CALC-TO-PROC" (static string), NULL if none.
    //   The primary user is an AI: a stable code says WHAT is wrong, a repair id says HOW to
    //   fix it — both machine-actionable, neither requiring prose parsing (REQ-0003). Set only
    //   at emit sites that KNOW the fix from context; never guessed post-hoc.
    const char    *repair;
    // ★★★ **어느 파일인가** (2026-08-06 · 단계 V). 한 단위가 여러 파일이고 **줄 번호는
    //   파일마다 1 부터 다시 시작**하므로, 줄만으로는 자리를 못 짚는다. NULL = 안 실림
    //   (아직 노드를 아는 자리에서만 싣는다 — 없는 것을 지어내지 않는다).
    const char    *file;
} low_diag_t;

#endif // LOW_DIAG_H
