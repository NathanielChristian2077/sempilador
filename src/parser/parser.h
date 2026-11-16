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
    PARSE_INVALID_INPUT,
    PARSE_NO_MEMORY,
    PARSE_LIMIT_EXCEEDED
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

typedef struct ParserStream ParserStream;

typedef struct {
    /* malloc-aligned allocations; context must outlive the stream. */
    void *context;
    void *(*allocate)(void *context, size_t bytes);
    void (*release)(void *context, void *memory);
} ParserAllocator;

typedef struct {
    /* Zero selects the default limit of 65536 pending symbols. */
    size_t max_stack;
    /* Both callbacks must be supplied, or both NULL to use malloc/free. */
    ParserAllocator allocator;
} ParserConfig;

typedef enum {
    PARSER_NEED_TOKEN,
    PARSER_FINISHED,
    PARSER_STOPPED,
    PARSER_PROTOCOL_ERROR
} ParserFlow;

typedef struct {
    ParserFlow flow;
    bool consumed;
} ParserStep;

/* Each instance has one consumer. Tokens are copied; lexeme bytes are borrowed.
 * create initializes *stream to NULL on failure; destroy accepts NULL.
 */
ParseStatus parser_stream_create(ParserStream **stream, const ParserConfig *config);
/* consumed means the token was accepted or discarded; never resend it.
 * FINISHED requires EOF, but recovered errors may remain in the result.
 * Calls after FINISHED report invalid input; calls after STOPPED preserve
 * the fatal result. Both return PROTOCOL_ERROR with consumed=false.
 */
ParserStep parser_stream_feed(ParserStream *stream, Token token);
/* A borrowed view valid until the next call or destruction. NULL returns NULL. */
const ParseResult *parser_stream_result(const ParserStream *stream);
/* Closing a waiting stream without EOF records INVALID_INPUT (expected EOF).
 * This is not a temporary-pause operation. Repeated finish calls are idempotent.
 */
ParseResult parser_stream_finish(ParserStream *stream);
void parser_stream_destroy(ParserStream *stream);

#endif
