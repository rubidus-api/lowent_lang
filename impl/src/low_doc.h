// low_doc.h — lowdoc prototype: render Markdown API docs from the shared front-end.
//
// For every `fn`/`proc` declaration it emits a Markdown section in the RFC-0033
// D12 canonical order (Summary · Description · Signature · Contract · Effects · Examples),
// pulling fields from the MVP clause syntax and prose from either a `lowdoc` clause or
// adjacent `rem` comments (rescanned from source, since comments are lexed away).
#ifndef LOW_DOC_H
#define LOW_DOC_H

#include "low_cst.h"
#include "proven/u8str.h"

// Render docs for all ops in `pr` to stdout. `work` runs the S4 passes whose verdicts
// (verified / violations) are injected as a per-op **Checks** section. Returns op count.
int low_doc(const low_parse_result_t *pr, proven_u8str_view_t src, proven_allocator_t work);

// Write <dir>/<module>.md (per module) + <dir>/llms.txt (curated index) to disk.
// Returns the op count, or -1 on an IO error.
int low_doc_write(const low_parse_result_t *pr, proven_u8str_view_t src, proven_allocator_t work, const char *dir);

#endif // LOW_DOC_H
