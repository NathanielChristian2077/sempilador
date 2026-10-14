#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../token/token.h"

typedef enum {
    PARSE_OK,
    PARSE_SYNTAX_ERROR,
    PARSE_LEXICAL_ERROR,
    PARSE_INVALID_INPUT
} ParseStatus;

typedef struct {
    size_t token_index;
    bool has_found;
    bool has_location;
    uint32_t line;
    uint32_t column;
    TokenKind found;
    bool expected[TOK_COMMENT + 1];
} ParseDiagnostic;

typedef struct {
    ParseStatus status;
    ParseDiagnostic diagnostic;
} ParseResult;

typedef struct {
    const Token *tokens;
    size_t count;
    size_t current;
    ParseResult result;
} Parser;

bool parser_init(Parser *parser, const Token *tokens, size_t count);
const Token *parser_peek(Parser *parser);
bool parser_expect(Parser *parser, TokenKind expected);

ParseResult parser_parse(const Token *tokens, size_t count);

bool parse_main(Parser *parser);

#endif
