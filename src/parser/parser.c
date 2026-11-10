#include "parser_internal.h"

void parser_add_error(ParseResult *result, ParseStatus status, size_t index,
                      const Token *token, uint64_t expected)
{
    if (result->diagnostic_count == PARSE_MAX_DIAGNOSTICS) return;
    result->status = status;
    ParseDiagnostic *d = &result->diagnostics[result->diagnostic_count++];
    *d = (ParseDiagnostic){.status = status, .token_index = index,
        .has_found = token != NULL,
        .has_location = token != NULL && token->line > 0 && token->column > 0,
        .line = token != NULL ? token->line : 0,
        .column = token != NULL ? token->column : 0,
        .found = token != NULL ? token->kind : TOK_INVALID};
    for (unsigned i = 0; i <= TOK_COMMENT; ++i)
        d->expected[i] = (expected & TOKEN_BIT(i)) != 0;
    if (result->diagnostic_count == PARSE_MAX_DIAGNOSTICS) result->limit_reached = true;
}

static bool cursor_error(Parser *p, ParseStatus status, const Token *token, uint64_t expected)
{
    if (!p->stopped && (!p->recovering || status != PARSE_SYNTAX_ERROR)) {
        parser_add_error(&p->result, status, p->current, token, expected);
        p->recovering = true;
        p->stopped = status != PARSE_SYNTAX_ERROR || p->result.limit_reached;
    }
    return false;
}

bool parser_init(Parser *p, const Token *tokens, size_t count)
{
    if (p == NULL) return false;
    *p = (Parser){.tokens = tokens, .count = count};
    if (tokens == NULL || count == 0) return cursor_error(p, PARSE_INVALID_INPUT, NULL, 0);
    return true;
}

const Token *parser_peek(Parser *p)
{
    if (p == NULL || p->tokens == NULL) return NULL;
    while (p->current < p->count && p->tokens[p->current].kind == TOK_COMMENT) ++p->current;
    if (p->current >= p->count) return NULL;
    const Token *token = &p->tokens[p->current];
    if ((unsigned)token->kind > TOK_COMMENT) cursor_error(p, PARSE_INVALID_INPUT, token, 0);
    else if (token->kind == TOK_INVALID) cursor_error(p, PARSE_LEXICAL_ERROR, token, 0);
    return token;
}

bool parser_expect(Parser *p, TokenKind expected)
{
    const Token *token = parser_peek(p);
    if (p == NULL || p->stopped || p->recovering) return false;
    if ((unsigned)expected > TOK_COMMENT) return cursor_error(p, PARSE_INVALID_INPUT, token, 0);
    if (token == NULL || token->kind != expected)
        return cursor_error(p, token == NULL ? PARSE_INVALID_INPUT : PARSE_SYNTAX_ERROR,
                            token, TOKEN_BIT(expected));
    if (token->kind != TOK_EOF) ++p->current;
    return true;
}

bool parse_main(Parser *p)
{
    if (p == NULL || p->stopped || p->recovering) return false;
    ParserStream stream;
    parser_stream_initialize(&stream, NULL, LL1_NT_PRINCIPAL, false);
    stream.next_index = p->current;
    while (p->current < p->count && stream.flow == PARSER_NEED_TOKEN) {
        Token token = p->tokens[p->current];
        parser_stream_feed(&stream, token);
        if (token.kind != TOK_EOF) ++p->current;
        else break;
    }
    p->result = parser_stream_finish(&stream);
    bool recognized = stream.flow == PARSER_FINISHED && stream.size == 0;
    p->stopped = stream.flow == PARSER_STOPPED;
    p->recovering = !recognized && p->result.status != PARSE_OK;
    parser_stream_dispose(&stream);
    return recognized;
}

bool parser_validate_tokens(const Token *tokens, size_t count, ParseResult *result)
{
    if (tokens == NULL || count == 0) {
        parser_add_error(result, PARSE_INVALID_INPUT, 0, NULL, 0);
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if ((unsigned)tokens[i].kind > TOK_COMMENT || (tokens[i].kind == TOK_EOF && i != count - 1)) {
            parser_add_error(result, PARSE_INVALID_INPUT, i, &tokens[i], 0);
            return false;
        }
    }
    if (tokens[count - 1].kind != TOK_EOF) {
        parser_add_error(result, PARSE_INVALID_INPUT, count, NULL, TOKEN_BIT(TOK_EOF));
        return false;
    }
    return true;
}

ParseResult parser_parse(const Token *tokens, size_t count)
{
    ParseResult result = {0};
    if (!parser_validate_tokens(tokens, count, &result)) return result;
    ParserStream stream;
    parser_stream_initialize(&stream, NULL, LL1_NT_PROGRAMA, true);
    for (size_t i = 0; i < count && stream.flow == PARSER_NEED_TOKEN; ++i)
        parser_stream_feed(&stream, tokens[i]);
    result = parser_stream_finish(&stream);
    parser_stream_dispose(&stream);
    return result;
}
