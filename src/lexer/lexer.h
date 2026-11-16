#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>
#include <stdint.h>

#include "../token/token.h"

typedef struct {
    const char *source;
    const char *current;
    const char *end;

    uint32_t line;
    uint32_t column;
} Lexer;

/* Borrow exactly length bytes until the caller has finished using all tokens.
 * No terminator or padding is required. NULL source denotes an empty input.
 * Positions are 1-based byte columns; CRLF counts as one line break.
 * Comments are returned; the parser skips them. EOF is repeatable.
 */
void lexer_init(
    Lexer *lexer,
    const char *source,
    size_t length
);

Token lexer_next(Lexer *lexer);

#endif
