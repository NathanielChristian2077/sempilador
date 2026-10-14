#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include "../token/token.h"

typedef struct {
    const Token *tokens;
    size_t count;
    size_t current;
} Parser;

bool parser_init(Parser *parser, const Token *tokens, size_t count);
const Token *parser_peek(Parser *parser);
bool parser_expect(Parser *parser, TokenKind expected);

bool parser_parse(const Token *tokens, size_t count);

bool parse_main(Parser *parser);

#endif
