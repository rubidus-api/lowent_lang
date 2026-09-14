// low_lex.h — L0 lexer for the lowent point-closure surface.
#ifndef LOW_LEX_H
#define LOW_LEX_H

#include "proven/types.h"
#include "proven/u8str.h"
#include "proven/array.h"
#include "low_token.h"
#include "low_diag.h"

typedef struct {
    proven_array_t tokens;   // of low_token_t (ends with a LOW_TOK_EOF)
    proven_array_t diags;    // of low_diag_t
    bool           ok;       // true iff no error-severity diagnostics
} low_lex_result_t;

// Tokenize `src` using `alloc` (arena-backed; lexeme views borrow `src`, so `src`
// must outlive the result). Newlines are non-significant (rev.d R7) but tracked for
// line/col. Comments (rem / note … TERM) are consumed and not emitted.
[[nodiscard]] low_lex_result_t low_lex(proven_allocator_t alloc, proven_u8str_view_t src);

#endif // LOW_LEX_H
