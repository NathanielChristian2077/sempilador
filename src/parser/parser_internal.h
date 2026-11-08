#ifndef SEMPILADOR_PARSER_INTERNAL_H
#define SEMPILADOR_PARSER_INTERNAL_H

#include "parser.h"
#include "generated/ll1_tables.h"

#define PARSER_CONTEXT_END UINT16_C(0x4000)
#define PARSER_INLINE_SYMBOLS 128
#define PARSER_INLINE_CONTEXTS 8
#define TOKEN_BIT(kind) (UINT64_C(1) << (kind))

typedef struct { size_t round, square, braces; } ParserDepth;
typedef enum { RECOVER_COMMAND, RECOVER_DECLARATION, RECOVER_FUNCTION } RecoveryKind;
typedef struct {
    size_t resume, start;
    ParserDepth depth;
    RecoveryKind kind;
    TokenKind first;
} ParserContext;
typedef enum { RECOVERY_NONE, RECOVERY_SKIP, RECOVERY_ID, RECOVERY_ELSE } RecoveryMode;

struct ParserStream {
    ParseResult result;
    ParserAllocator allocator;
    LL1Symbol *stack;
    size_t size, capacity, max_stack;
    ParserContext *contexts;
    size_t context_count, context_capacity;
    LL1Symbol inline_stack[PARSER_INLINE_SYMBOLS];
    ParserContext inline_contexts[PARSER_INLINE_CONTEXTS];
    ParserDepth depth;
    ParserContext recovery;
    RecoveryMode mode;
    Token pending_id;
    size_t pending_index, next_index, index;
    bool else_seen;
    ParserFlow flow;
};

void parser_add_error(ParseResult *result, ParseStatus status, size_t index,
                      const Token *token, uint64_t expected);
ParseStatus parser_stream_initialize(ParserStream *p, const ParserConfig *config,
                                     LL1Nonterminal start, bool require_eof);
void parser_stream_dispose(ParserStream *p);
bool parser_reserve(ParserStream *p, size_t symbols, size_t contexts, const Token *token);
void parser_resource_error(ParserStream *p, ParseStatus status, const Token *token);
void parser_track(ParserStream *p, TokenKind kind);
bool parser_begin_recovery(ParserStream *p, const Token *token, LL1Symbol failed);
/* true means token was consumed by recovery; false retries it in normal mode. */
bool parser_recover_token(ParserStream *p, Token token);
void parser_process_token(ParserStream *p, Token token);

#endif
