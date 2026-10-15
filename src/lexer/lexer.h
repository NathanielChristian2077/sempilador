#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "../token/token.h"

typedef struct {
    const char *source;
    const char *current;
    const char *end;

    uint32_t line;
    uint32_t column;
} Lexer;

void lexer_init(
    Lexer *lexer,
    const char *source,
    size_t length
);

Token lexer_next(Lexer *lexer);

#endif
