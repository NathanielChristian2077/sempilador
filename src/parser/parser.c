#include "parser.h"

static bool parse_block(Parser *parser);
static bool parse_expression(Parser *parser);
static bool parse_additive(Parser *parser);
static bool parse_factor(Parser *parser);

/* Initialize the cursor with a borrowed token array. */
bool parser_init(Parser *parser, const Token *tokens, size_t count)
{
    if (parser == NULL || tokens == NULL || count == 0)
        return false;
    *parser = (Parser){ .tokens = tokens, .count = count, .current = 0 };
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
    return &parser->tokens[parser->current];
}

/* Consume the expected token, keeping EOF in place. */
bool parser_expect(Parser *parser, TokenKind expected)
{
    const Token *token = parser_peek(parser);
    if (token == NULL || token->kind == TOK_INVALID || token->kind != expected)
        return false;
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

static bool start_type(TokenKind kind)
{
    return kind == TOK_CHAR || kind == TOK_FLOAT ||
           kind == TOK_INT || kind == TOK_BOOLEAN;
}

static bool parse_base_type(Parser *parser)
{
    const Token *token = parser_peek(parser);
    return token != NULL && start_type(token->kind) && consume(parser);
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
        if (!parse_type(parser) || !parser_expect(parser, TOK_ID) ||
            !parse_id_list(parser) || !parser_expect(parser, TOK_SEMICOLON))
            return false;
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
    return parser_expect(parser, TOK_NUM_INT) || parser_expect(parser, TOK_NUM_DEC);
}

static bool parse_term(Parser *parser)
{
    if (at(parser, TOK_ID))
        return parser_expect(parser, TOK_ID) && parse_indices(parser);
    return parse_constant(parser);
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
    }
    return parse_term(parser);
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
    } else if (!parse_expression(parser)) {
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
    return parse_constant(parser);
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
        return false;
    switch (token->kind) {
    case TOK_SCANF: return parse_read(parser);
    case TOK_PRINTLN: return parse_write(parser);
    case TOK_ID: return parse_assignment(parser);
    case TOK_FUNC: return parse_call(parser);
    case TOK_IF: return parse_if(parser);
    case TOK_WHILE: return parse_while(parser);
    case TOK_FOR: return parse_for(parser);
    case TOK_RETURN: return parse_return(parser);
    default: return false;
    }
}

static bool parse_commands(Parser *parser)
{
    while (!at(parser, TOK_RBRACE)) {
        if (!parse_command(parser))
            return false;
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
    while (token != NULL && (start_type(token->kind) || token->kind == TOK_VOID)) {
        if (!parse_function(parser))
            return false;
        token = parser_peek(parser);
    }
    return parse_main(parser) && parser_expect(parser, TOK_EOF);
}

/* Validate the token array and recognize a complete program. */
bool parser_parse(const Token *tokens, size_t count)
{
    Parser parser;
    if (!parser_init(&parser, tokens, count) || tokens[count - 1].kind != TOK_EOF)
        return false;

    for (size_t i = 0; i < count; ++i) {
        if ((unsigned)tokens[i].kind > (unsigned)TOK_COMMENT ||
            tokens[i].kind == TOK_INVALID ||
            (tokens[i].kind == TOK_EOF && i != count - 1))
            return false;
    }
    return parse_program(&parser);
}
