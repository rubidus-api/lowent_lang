// External scanner for tree-sitter-lowent: the two tokens whose end is named by the text itself.
//
//   note TAG …            block comment — runs to the line that holds TAG alone, starting in column 0
//   text TAG [u|U] …      text literal  — same closing rule; the body is kept as written
//
// Both open with a word, a blank and a tag (Annex A.10.1: block-comment, text-literal).
#include "tree_sitter/parser.h"
#include <stdbool.h>
#include <stdint.h>

enum TokenType { BLOCK_COMMENT, TEXT_LITERAL, ERROR_SENTINEL };

void *tree_sitter_lowent_external_scanner_create(void) { return NULL; }
void tree_sitter_lowent_external_scanner_destroy(void *p) { (void)p; }
unsigned tree_sitter_lowent_external_scanner_serialize(void *p, char *b) { (void)p; (void)b; return 0; }
void tree_sitter_lowent_external_scanner_deserialize(void *p, const char *b, unsigned n) { (void)p; (void)b; (void)n; }

static bool is_name_char(int32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
static bool is_blank(int32_t c) { return c == ' ' || c == '\t' || c == '\r'; }

#define TAG_MAX 64

bool tree_sitter_lowent_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid) {
    (void)payload;
    if (valid[ERROR_SENTINEL]) return false;              // error recovery: let the grammar's own tokens speak
    if (!valid[BLOCK_COMMENT] && !valid[TEXT_LITERAL]) return false;

    while (lexer->lookahead == ' ' || lexer->lookahead == '\t' || lexer->lookahead == '\r' || lexer->lookahead == '\n')
        lexer->advance(lexer, true);

    const char *word; enum TokenType kind;
    if (lexer->lookahead == 'n')      { word = "note"; kind = BLOCK_COMMENT; }
    else if (lexer->lookahead == 't') { word = "text"; kind = TEXT_LITERAL; }
    else return false;
    if (!valid[kind]) return false;
    for (const char *p = word; *p; p++) {
        if (lexer->lookahead != (int32_t)*p) return false;
        lexer->advance(lexer, false);
    }
    if (lexer->lookahead != ' ' && lexer->lookahead != '\t') return false;   // `notes`, `text.` … are ordinary names
    while (is_blank(lexer->lookahead)) lexer->advance(lexer, false);

    int32_t tag[TAG_MAX]; unsigned n = 0;
    while (is_name_char(lexer->lookahead)) {
        if (n == TAG_MAX) return false;
        tag[n++] = lexer->lookahead;
        lexer->advance(lexer, false);
    }
    if (n == 0) return false;
    while (lexer->lookahead != '\n' && !lexer->eof(lexer)) lexer->advance(lexer, false);   // the rest of the opening line

    for (;;) {                                            // one line at a time
        if (lexer->eof(lexer)) return false;              // never closed — not this token
        lexer->advance(lexer, false);                     // the newline
        unsigned i = 0;
        while (i < n && lexer->lookahead == tag[i]) { lexer->advance(lexer, false); i++; }
        if (i == n && !is_name_char(lexer->lookahead)) {
            while (is_blank(lexer->lookahead)) lexer->advance(lexer, false);
            if (lexer->lookahead == '\n' || lexer->eof(lexer)) {
                lexer->mark_end(lexer);
                lexer->result_symbol = kind;
                return true;
            }
        }
        while (lexer->lookahead != '\n' && !lexer->eof(lexer)) lexer->advance(lexer, false);
    }
}
