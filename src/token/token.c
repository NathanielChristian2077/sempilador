
#include "token.h"

static const char *const token_names[] = {
    [TOK_EOF]       = "TOK_EOF",
    [TOK_INVALID]   = "TOK_INVALID",

    [TOK_ID]        = "TOK_ID",
    [TOK_NUM_INT]   = "TOK_NUM_INT",
    [TOK_NUM_DEC]   = "TOK_NUM_DEC",
    [TOK_TEXTO]     = "TOK_TEXTO",

    [TOK_INT]       = "TOK_INT",
    [TOK_FLOAT]     = "TOK_FLOAT",
    [TOK_CHAR]      = "TOK_CHAR",
    [TOK_BOOLEAN]   = "TOK_BOOLEAN",
    [TOK_VOID]      = "TOK_VOID",
    [TOK_IF]        = "TOK_IF",
    [TOK_ELSE]      = "TOK_ELSE",
    [TOK_FOR]       = "TOK_FOR",
    [TOK_WHILE]     = "TOK_WHILE",
    [TOK_SCANF]     = "TOK_SCANF",
    [TOK_PRINTLN]   = "TOK_PRINTLN",
    [TOK_MAIN]      = "TOK_MAIN",
    [TOK_RETURN]    = "TOK_RETURN",
    [TOK_FUNC]      = "TOK_FUNC",

    [TOK_ASSIGN]    = "TOK_ASSIGN",
    [TOK_PLUS]      = "TOK_PLUS",
    [TOK_MINUS]     = "TOK_MINUS",
    [TOK_STAR]      = "TOK_STAR",
    [TOK_SLASH]     = "TOK_SLASH",
    [TOK_PERCENT]   = "TOK_PERCENT",
    [TOK_AND]       = "TOK_AND",
    [TOK_OR]        = "TOK_OR",
    [TOK_NOT]       = "TOK_NOT",
    [TOK_COMP]      = "TOK_COMP",

    [TOK_LPAREN]    = "TOK_LPAREN",
    [TOK_RPAREN]    = "TOK_RPAREN",
    [TOK_LBRACKET]  = "TOK_LBRACKET",
    [TOK_RBRACKET]  = "TOK_RBRACKET",
    [TOK_LBRACE]    = "TOK_LBRACE",
    [TOK_RBRACE]    = "TOK_RBRACE",
    [TOK_COMMA]     = "TOK_COMMA",
    [TOK_SEMICOLON] = "TOK_SEMICOLON",

    [TOK_COMMENT]   = "TOK_COMMENT"
};

const char *token_kind_name(TokenKind kind) {
    size_t count = sizeof(token_names)/ sizeof(token_names[0]);

    if ((unsigned)kind >= count)
        return "TOK_UNKNOWN";

    return token_names[kind] ? token_names[kind] : "TOK_UNKNOWN";
}
