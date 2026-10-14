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
    ParseStatus status;
    size_t token_index;
    bool has_found;
    bool has_location;
    uint32_t line;
    uint32_t column;
    TokenKind found;
    bool expected[TOK_COMMENT + 1];
} ParseDiagnostic;

#define PARSE_MAX_DIAGNOSTICS 32

typedef struct {
    ParseStatus status;
    size_t diagnostic_count;
    ParseDiagnostic diagnostics[PARSE_MAX_DIAGNOSTICS];
    bool limit_reached;
} ParseResult;

typedef struct {
    const Token *tokens;
    size_t count;
    size_t current;
    ParseResult result;
    bool recovering;
    bool stopped;
} Parser;

bool parser_init(Parser *parser, const Token *tokens, size_t count);
const Token *parser_peek(Parser *parser);
bool parser_expect(Parser *parser, TokenKind expected);

ParseResult parser_parse(const Token *tokens, size_t count);

bool parse_main(Parser *parser);

#endif
