#include "parser.h"

static bool parse_block(Parser *parser);
static bool parse_expression(Parser *parser);
static bool parse_additive(Parser *parser);
static bool parse_factor(Parser *parser);

typedef enum {
    RECOVER_DECLARATION,
    RECOVER_COMMAND,
    RECOVER_FUNCTION
} RecoveryContext;

static bool synchronize(Parser *parser, size_t start, RecoveryContext context);

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const TokenKind type_starts[] = {
    TOK_CHAR, TOK_FLOAT, TOK_INT, TOK_BOOLEAN
};
static const TokenKind factor_starts[] = {
    TOK_ID, TOK_NUM_INT, TOK_NUM_DEC, TOK_PLUS, TOK_MINUS,
    TOK_TEXTO, TOK_NOT, TOK_LPAREN
};
static const TokenKind command_follow[] = {
    TOK_SCANF, TOK_PRINTLN, TOK_ID, TOK_FUNC, TOK_IF, TOK_WHILE,
    TOK_FOR, TOK_RETURN, TOK_RBRACE
};
static const TokenKind function_follow[] = {
    TOK_CHAR, TOK_FLOAT, TOK_INT, TOK_BOOLEAN, TOK_VOID, TOK_MAIN
};

static bool record_error(Parser *parser, ParseStatus status,
                         const Token *token)
{
    if (parser != NULL && !parser->stopped &&
        (!parser->recovering || status != PARSE_SYNTAX_ERROR)) {
        parser->result.status = status;
        parser->result.diagnostics[parser->result.diagnostic_count++] = (ParseDiagnostic){
            .status = status,
            .token_index = parser->current,
            .has_found = token != NULL,
            .has_location = token != NULL && token->line > 0 && token->column > 0,
            .line = token != NULL ? token->line : 0,
            .column = token != NULL ? token->column : 0,
            .found = token != NULL ? token->kind : TOK_INVALID
        };
        parser->recovering = true;
        if (status != PARSE_SYNTAX_ERROR)
            parser->stopped = true;
        if (parser->result.diagnostic_count == PARSE_MAX_DIAGNOSTICS) {
            parser->result.limit_reached = true;
            parser->stopped = true;
        }
    }
    return false;
}

static bool fail_expected(Parser *parser, const TokenKind *expected, size_t count)
{
    const Token *token = parser_peek(parser);
    if (parser == NULL || parser->stopped || parser->recovering)
        return false;
    record_error(parser, token != NULL ? PARSE_SYNTAX_ERROR : PARSE_INVALID_INPUT,
                 token);
    for (size_t i = 0; i < count; ++i)
        parser->result.diagnostics[parser->result.diagnostic_count - 1].expected[expected[i]] = true;
    return false;
}

/* Initialize the cursor with a borrowed token array. */
bool parser_init(Parser *parser, const Token *tokens, size_t count)
{
    if (parser == NULL)
        return false;
    *parser = (Parser){ .tokens = tokens, .count = count, .current = 0 };
    if (tokens == NULL || count == 0)
        return record_error(parser, PARSE_INVALID_INPUT, NULL);
    return true;
}

/* Return the current token, skipping comments. */
const Token *parser_peek(Parser *parser)
{
    if (parser == NULL || parser->tokens == NULL)
        return NULL;
    while (parser->current < parser->count &&
           parser->tokens[parser->current].kind == TOK_COMMENT)
        ++parser->current;
    if (parser->current >= parser->count)
        return NULL;
    const Token *token = &parser->tokens[parser->current];
    if ((unsigned)token->kind > (unsigned)TOK_COMMENT)
        record_error(parser, PARSE_INVALID_INPUT, token);
    else if (token->kind == TOK_INVALID)
        record_error(parser, PARSE_LEXICAL_ERROR, token);
    return token;
}

/* Consume the expected token, keeping EOF in place. */
bool parser_expect(Parser *parser, TokenKind expected)
{
    const Token *token = parser_peek(parser);
    if (parser == NULL || parser->stopped || parser->recovering)
        return false;
    if ((unsigned)expected > (unsigned)TOK_COMMENT)
        return record_error(parser, PARSE_INVALID_INPUT, token);
    if (token == NULL || token->kind != expected)
        return fail_expected(parser, &expected, 1);
    if (token->kind != TOK_EOF)
        ++parser->current;
    return true;
}

/* Check the current token without consuming it. */
static bool at(Parser *parser, TokenKind kind)
{
    const Token *token = parser_peek(parser);
    return token != NULL && token->kind == kind;
}

/* Consume the current token, keeping EOF in place. */
static bool consume(Parser *parser)
{
    const Token *token = parser_peek(parser);
    return token != NULL && parser_expect(parser, token->kind);
}

static bool matches_any(Parser *parser, const TokenKind *kinds, size_t count)
{
    const Token *token = parser_peek(parser);
    if (token == NULL)
        return false;
    for (size_t i = 0; i < count; ++i) {
        if (token->kind == kinds[i])
            return true;
    }
    return false;
}

static bool start_type(TokenKind kind)
{
    return kind == TOK_CHAR || kind == TOK_FLOAT ||
           kind == TOK_INT || kind == TOK_BOOLEAN;
}

static bool in_set(TokenKind kind, const TokenKind *set, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (kind == set[i])
            return true;
    }
    return false;
}

static bool recovery_candidate(TokenKind kind, RecoveryContext context)
{
    if (context == RECOVER_FUNCTION)
        return in_set(kind, function_follow, COUNT(function_follow));
    if (context == RECOVER_DECLARATION && start_type(kind))
        return true;
    return in_set(kind, command_follow, COUNT(command_follow));
}

static bool assignment_ahead(const Parser *parser)
{
    size_t next = parser->current + 1;
    while (next < parser->count && parser->tokens[next].kind == TOK_COMMENT)
        ++next;
    return next < parser->count && parser->tokens[next].kind == TOK_ASSIGN;
}

static void track_delimiter(TokenKind kind, size_t *round, size_t *square,
                            size_t *braces)
{
    switch (kind) {
    case TOK_LPAREN: ++*round; break;
    case TOK_RPAREN: if (*round > 0) --*round; break;
    case TOK_LBRACKET: ++*square; break;
    case TOK_RBRACKET: if (*square > 0) --*square; break;
    case TOK_LBRACE:
        if (*braces == 0) {
            *round = 0;
            *square = 0;
        }
        ++*braces;
        break;
    case TOK_RBRACE: if (*braces > 0) --*braces; break;
    default: break;
    }
}

/* Resume at a context-specific FIRST/FOLLOW boundary. */
static bool synchronize(Parser *parser, size_t start, RecoveryContext context)
{
    if (parser->stopped)
        return false;
    size_t round = 0, square = 0, braces = 0;
    for (size_t i = start; i < parser->current; ++i)
        track_delimiter(parser->tokens[i].kind, &round, &square, &braces);
    TokenKind first = parser->tokens[start].kind;
    bool control = first == TOK_FOR || first == TOK_WHILE || first == TOK_IF;

    for (;;) {
        const Token *token = parser_peek(parser);
        if (parser->stopped || token == NULL)
            return false;
        TokenKind kind = token->kind;
        if (kind == TOK_EOF) {
            parser->recovering = false;
            return true;
        }
        if (kind == TOK_MAIN) {
            if (context != RECOVER_FUNCTION)
                return false;
            parser->recovering = false;
            return true;
        }
        if (kind == TOK_RBRACE && braces == 0 && context != RECOVER_FUNCTION) {
            parser->recovering = false;
            return true;
        }
        if (braces == 0 && round == 0 && square == 0 &&
            parser->current > start && recovery_candidate(kind, context) &&
            (kind != TOK_ID || assignment_ahead(parser))) {
            parser->recovering = false;
            return true;
        }
        if (kind == TOK_SEMICOLON && braces == 0 &&
            (context == RECOVER_DECLARATION || (!control && context != RECOVER_FUNCTION) ||
             (round == 0 && square == 0))) {
            ++parser->current;
            parser->recovering = false;
            return true;
        }
        bool closes_body = kind == TOK_RBRACE && braces == 1;
        track_delimiter(kind, &round, &square, &braces);
        ++parser->current;
        if (closes_body) {
            if (first == TOK_IF && at(parser, TOK_ELSE)) {
                ++parser->current;
                continue;
            }
            parser->recovering = false;
            return !parser->stopped;
        }
    }
}

static bool parse_base_type(Parser *parser)
{
    const Token *token = parser_peek(parser);
    if (token != NULL && start_type(token->kind))
        return consume(parser);
    return fail_expected(parser, type_starts, COUNT(type_starts));
}

static bool parse_dimension(Parser *parser)
{
    while (at(parser, TOK_LBRACKET)) {
        if (!parser_expect(parser, TOK_LBRACKET) ||
            !parser_expect(parser, TOK_NUM_INT) ||
            !parser_expect(parser, TOK_RBRACKET))
            return false;
    }

    return true;
}

static bool parse_type(Parser *parser)
{
    return parse_base_type(parser) && parse_dimension(parser);
}

static bool parse_id_list(Parser *parser)
{
    while (at(parser, TOK_COMMA)) {
        if (!parser_expect(parser, TOK_COMMA) || !parser_expect(parser, TOK_ID))
            return false;
    }
    return true;
}

static bool parse_variables_list(Parser *parser)
{
    const Token *token = parser_peek(parser);
    while (token != NULL && start_type(token->kind)) {
        size_t start = parser->current;
        if (!parse_type(parser) || !parser_expect(parser, TOK_ID) ||
            !parse_id_list(parser) || !parser_expect(parser, TOK_SEMICOLON)) {
            if (!synchronize(parser, start, RECOVER_DECLARATION))
                return false;
        }
        token = parser_peek(parser);
    }
    return true;
}

static bool parse_indices(Parser *parser)
{
    while (at(parser, TOK_LBRACKET)) {
        if (!parser_expect(parser, TOK_LBRACKET) || !parse_additive(parser) ||
            !parser_expect(parser, TOK_RBRACKET))
            return false;
    }
    return true;
}

static bool parse_constant(Parser *parser)
{
    if (at(parser, TOK_NUM_INT) || at(parser, TOK_NUM_DEC))
        return consume(parser);
    const TokenKind expected[] = {TOK_NUM_INT, TOK_NUM_DEC};
    return fail_expected(parser, expected, COUNT(expected));
}

static bool parse_term(Parser *parser)
{
    if (at(parser, TOK_ID))
        return parser_expect(parser, TOK_ID) && parse_indices(parser);
    if (at(parser, TOK_NUM_INT) || at(parser, TOK_NUM_DEC))
        return parse_constant(parser);
    const TokenKind expected[] = {TOK_ID, TOK_NUM_INT, TOK_NUM_DEC};
    return fail_expected(parser, expected, COUNT(expected));
}

static bool parse_factor(Parser *parser)
{
    if (at(parser, TOK_TEXTO))
        return parser_expect(parser, TOK_TEXTO);
    if (at(parser, TOK_NOT))
        return parser_expect(parser, TOK_NOT) && parse_factor(parser);
    if (at(parser, TOK_LPAREN))
        return parser_expect(parser, TOK_LPAREN) && parse_expression(parser) &&
               parser_expect(parser, TOK_RPAREN);
    if (at(parser, TOK_PLUS) || at(parser, TOK_MINUS)) {
        if (!consume(parser))
            return false;
        return parse_term(parser);
    }
    if (at(parser, TOK_ID) || at(parser, TOK_NUM_INT) || at(parser, TOK_NUM_DEC))
        return parse_term(parser);
    return fail_expected(parser, factor_starts, COUNT(factor_starts));
}

static bool parse_multiplicative(Parser *parser)
{
    if (!parse_factor(parser))
        return false;
    while (at(parser, TOK_STAR) || at(parser, TOK_SLASH) || at(parser, TOK_PERCENT)) {
        if (!consume(parser) || !parse_factor(parser))
            return false;
    }
    return true;
}

static bool parse_additive(Parser *parser)
{
    if (!parse_multiplicative(parser))
        return false;
    while (at(parser, TOK_PLUS) || at(parser, TOK_MINUS)) {
        if (!consume(parser) || !parse_multiplicative(parser))
            return false;
    }
    return true;
}

static bool parse_relational(Parser *parser)
{
    if (!parse_additive(parser))
        return false;

    if (at(parser, TOK_COMP))
        return parser_expect(parser, TOK_COMP) && parse_additive(parser);
    return true;
}

static bool parse_and(Parser *parser)
{
    if (!parse_relational(parser))
        return false;
    while (at(parser, TOK_AND)) {
        if (!parser_expect(parser, TOK_AND) || !parse_relational(parser))
            return false;
    }
    return true;
}

static bool parse_expression(Parser *parser)
{
    if (!parse_and(parser))
        return false;
    while (at(parser, TOK_OR)) {
        if (!parser_expect(parser, TOK_OR) || !parse_and(parser))
            return false;
    }
    return true;
}

static bool parse_arguments(Parser *parser)
{
    if (at(parser, TOK_RPAREN))
        return true;
    if (!matches_any(parser, factor_starts, COUNT(factor_starts))) {
        const TokenKind expected[] = {
            TOK_ID, TOK_NUM_INT, TOK_NUM_DEC, TOK_PLUS, TOK_MINUS,
            TOK_TEXTO, TOK_NOT, TOK_LPAREN, TOK_RPAREN
        };
        return fail_expected(parser, expected, COUNT(expected));
    }
    if (!parse_expression(parser))
        return false;
    while (at(parser, TOK_COMMA)) {
        if (!parser_expect(parser, TOK_COMMA) || !parse_expression(parser))
            return false;
    }
    return true;
}

static bool parse_call(Parser *parser)
{
    return parser_expect(parser, TOK_FUNC) && parser_expect(parser, TOK_ID) &&
           parser_expect(parser, TOK_LPAREN) && parse_arguments(parser) &&
           parser_expect(parser, TOK_RPAREN);
}

static bool parse_assignment(Parser *parser)
{
    if (!parser_expect(parser, TOK_ID) || !parser_expect(parser, TOK_ASSIGN))
        return false;
    if (at(parser, TOK_FUNC)) {
        if (!parse_call(parser))
            return false;
    } else {
        if (!matches_any(parser, factor_starts, COUNT(factor_starts))) {
            const TokenKind expected[] = {
                TOK_ID, TOK_NUM_INT, TOK_NUM_DEC, TOK_PLUS, TOK_MINUS,
                TOK_TEXTO, TOK_NOT, TOK_LPAREN, TOK_FUNC
            };
            return fail_expected(parser, expected, COUNT(expected));
        }
        if (!parse_expression(parser))
            return false;
    }
    return parser_expect(parser, TOK_SEMICOLON);
}

static bool parse_read_term(Parser *parser)
{
    return parser_expect(parser, TOK_ID) && parse_indices(parser);
}

static bool parse_write_term(Parser *parser)
{
    if (at(parser, TOK_ID))
        return parse_read_term(parser);
    if (at(parser, TOK_TEXTO))
        return parser_expect(parser, TOK_TEXTO);
    if (at(parser, TOK_NUM_INT) || at(parser, TOK_NUM_DEC))
        return parse_constant(parser);
    const TokenKind expected[] = {TOK_ID, TOK_NUM_INT, TOK_NUM_DEC, TOK_TEXTO};
    return fail_expected(parser, expected, COUNT(expected));
}

static bool parse_read(Parser *parser)
{
    if (!parser_expect(parser, TOK_SCANF) || !parser_expect(parser, TOK_LPAREN) ||
        !parse_read_term(parser))
        return false;
    while (at(parser, TOK_COMMA)) {
        if (!parser_expect(parser, TOK_COMMA) || !parse_read_term(parser))
            return false;
    }
    return parser_expect(parser, TOK_RPAREN) && parser_expect(parser, TOK_SEMICOLON);
}

static bool parse_write(Parser *parser)
{
    if (!parser_expect(parser, TOK_PRINTLN) || !parser_expect(parser, TOK_LPAREN) ||
        !parse_write_term(parser))
        return false;
    while (at(parser, TOK_COMMA)) {
        if (!parser_expect(parser, TOK_COMMA) || !parse_write_term(parser))
            return false;
    }
    return parser_expect(parser, TOK_RPAREN) && parser_expect(parser, TOK_SEMICOLON);
}

static bool parse_if(Parser *parser)
{
    if (!parser_expect(parser, TOK_IF) || !parser_expect(parser, TOK_LPAREN) ||
        !parse_expression(parser) || !parser_expect(parser, TOK_RPAREN) ||
        !parse_block(parser))
        return false;
    if (at(parser, TOK_ELSE))
        return parser_expect(parser, TOK_ELSE) && parse_block(parser);
    return true;
}

static bool parse_while(Parser *parser)
{
    return parser_expect(parser, TOK_WHILE) && parser_expect(parser, TOK_LPAREN) &&
           parse_expression(parser) && parser_expect(parser, TOK_RPAREN) &&
           parse_block(parser);
}

static bool parse_for_assignment(Parser *parser, TokenKind end)
{
    if (at(parser, end))
        return true;
    if (!at(parser, TOK_ID)) {
        const TokenKind expected[] = {TOK_ID, end};
        return fail_expected(parser, expected, COUNT(expected));
    }
    return parser_expect(parser, TOK_ID) && parser_expect(parser, TOK_ASSIGN) &&
           parse_expression(parser);
}

static bool parse_for(Parser *parser)
{
    if (!parser_expect(parser, TOK_FOR) || !parser_expect(parser, TOK_LPAREN) ||
        !parse_for_assignment(parser, TOK_SEMICOLON) ||
        !parser_expect(parser, TOK_SEMICOLON))
        return false;
    if (!at(parser, TOK_SEMICOLON) && !parse_expression(parser))
        return false;
    return parser_expect(parser, TOK_SEMICOLON) &&
           parse_for_assignment(parser, TOK_RPAREN) &&
           parser_expect(parser, TOK_RPAREN) && parse_block(parser);
}

static bool parse_return(Parser *parser)
{
    return parser_expect(parser, TOK_RETURN) && parse_expression(parser) &&
           parser_expect(parser, TOK_SEMICOLON);
}

static bool parse_command(Parser *parser)
{
    const Token *token = parser_peek(parser);
    if (token == NULL)
        return fail_expected(parser, command_follow, COUNT(command_follow));
    switch (token->kind) {
    case TOK_SCANF: return parse_read(parser);
    case TOK_PRINTLN: return parse_write(parser);
    case TOK_ID: return parse_assignment(parser);
    case TOK_FUNC: return parse_call(parser);
    case TOK_IF: return parse_if(parser);
    case TOK_WHILE: return parse_while(parser);
    case TOK_FOR: return parse_for(parser);
    case TOK_RETURN: return parse_return(parser);
    default: return fail_expected(parser, command_follow, COUNT(command_follow));
    }
}

static bool parse_commands(Parser *parser)
{
    while (!at(parser, TOK_RBRACE)) {
        size_t start = parser->current;
        if (at(parser, TOK_EOF))
            return parser_expect(parser, TOK_RBRACE);
        if (!parse_command(parser)) {
            if (!synchronize(parser, start, RECOVER_COMMAND))
                return false;
        }
    }
    return true;
}

static bool parse_block(Parser *parser)
{
    return parser_expect(parser, TOK_LBRACE) && parse_variables_list(parser) &&
           parse_commands(parser) && parser_expect(parser, TOK_RBRACE);
}

static bool parse_parameters(Parser *parser)
{
    if (at(parser, TOK_RPAREN))
        return true;
    const TokenKind expected[] = {
        TOK_CHAR, TOK_FLOAT, TOK_INT, TOK_BOOLEAN, TOK_RPAREN
    };
    if (!matches_any(parser, type_starts, COUNT(type_starts)))
        return fail_expected(parser, expected, COUNT(expected));
    if (!parse_type(parser) || !parser_expect(parser, TOK_ID))
        return false;
    while (at(parser, TOK_COMMA)) {
        if (!parser_expect(parser, TOK_COMMA) || !parse_type(parser) ||
            !parser_expect(parser, TOK_ID))
            return false;
    }
    return true;
}

static bool parse_function(Parser *parser)
{
    if (at(parser, TOK_VOID)) {
        if (!parser_expect(parser, TOK_VOID))
            return false;
    } else if (!parse_type(parser)) {
        return false;
    }
    return parser_expect(parser, TOK_ID) && parser_expect(parser, TOK_LPAREN) &&
           parse_parameters(parser) && parser_expect(parser, TOK_RPAREN) &&
           parse_block(parser);
}

bool parse_main(Parser *parser)
{
    return parser_expect(parser, TOK_MAIN) && parser_expect(parser, TOK_LPAREN) &&
           parser_expect(parser, TOK_RPAREN) && parse_block(parser);
}

static bool parse_program(Parser *parser)
{
    const Token *token = parser_peek(parser);
    while (token != NULL && token->kind != TOK_MAIN && token->kind != TOK_EOF) {
        size_t start = parser->current;
        bool recognized = false;
        if (start_type(token->kind) || token->kind == TOK_VOID) {
            recognized = parse_function(parser);
        } else {
            const TokenKind expected[] = {
                TOK_CHAR, TOK_FLOAT, TOK_INT, TOK_BOOLEAN, TOK_VOID, TOK_MAIN
            };
            fail_expected(parser, expected, COUNT(expected));
        }
        if (!recognized && !synchronize(parser, start, RECOVER_FUNCTION))
            return false;
        token = parser_peek(parser);
    }
    if (!at(parser, TOK_MAIN)) {
        const TokenKind expected[] = {
            TOK_CHAR, TOK_FLOAT, TOK_INT, TOK_BOOLEAN, TOK_VOID, TOK_MAIN
        };
        return fail_expected(parser, expected, COUNT(expected));
    }
    return parse_main(parser) && parser_expect(parser, TOK_EOF);
}

/* Validate the token array and collect bounded recognition diagnostics. */
ParseResult parser_parse(const Token *tokens, size_t count)
{
    Parser parser = {0};
    if (!parser_init(&parser, tokens, count))
        return parser.result;

    for (size_t i = 0; i < count; ++i) {
        if ((unsigned)tokens[i].kind > (unsigned)TOK_COMMENT ||
            (tokens[i].kind == TOK_EOF && i != count - 1)) {
            parser.current = i;
            record_error(&parser, PARSE_INVALID_INPUT, &tokens[i]);
            return parser.result;
        }
    }
    if (tokens[count - 1].kind != TOK_EOF) {
        parser.current = count;
        record_error(&parser, PARSE_INVALID_INPUT, NULL);
        parser.result.diagnostics[0].expected[TOK_EOF] = true;
        return parser.result;
    }
    parse_program(&parser);
    return parser.result;
}
