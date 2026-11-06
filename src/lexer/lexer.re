/* Source of lexer.c. Regenerate with `make regen-lexer`; do not edit lexer.c. */
#include "lexer.h"

#include <string.h>

static uint32_t increment_position(uint32_t value)
{
    return value == UINT32_MAX ? value : value + 1;
}

/* Commit only the matched span: re2c may backtrack while recognizing a token. */
static Token finish(Lexer *lexer, const char *end, TokenKind kind)
{
    Token token = {kind, lexer->current, (size_t)(end - lexer->current),
                   lexer->line, lexer->column};
    while (lexer->current != end) {
        const char *position = lexer->current++;
        if (*position == '\r') {
            lexer->line = increment_position(lexer->line);
            lexer->column = 1;
        } else if (*position == '\n') {
            if (position == lexer->source || position[-1] != '\r')
                lexer->line = increment_position(lexer->line);
            lexer->column = 1;
        } else {
            lexer->column = increment_position(lexer->column);
        }
    }
    return token;
}

static TokenKind keyword_kind(const char *text, size_t length)
{
    static const struct { const char *text; size_t length; TokenKind kind; } words[] = {
        {"int", 3, TOK_INT}, {"float", 5, TOK_FLOAT}, {"char", 4, TOK_CHAR},
        {"boolean", 7, TOK_BOOLEAN}, {"void", 4, TOK_VOID}, {"if", 2, TOK_IF},
        {"else", 4, TOK_ELSE}, {"for", 3, TOK_FOR}, {"while", 5, TOK_WHILE},
        {"scanf", 5, TOK_SCANF}, {"println", 7, TOK_PRINTLN}, {"main", 4, TOK_MAIN},
        {"return", 6, TOK_RETURN}, {"func", 4, TOK_FUNC}
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        if (length == words[i].length && memcmp(text, words[i].text, length) == 0)
            return words[i].kind;
    return TOK_ID;
}

void lexer_init(Lexer *lexer, const char *source, size_t length)
{
    static const char empty[] = "";
    if (source == NULL) {
        source = empty;
        length = 0;
    }
    *lexer = (Lexer){source, source, source + length, 1, 1};
}

Token lexer_next(Lexer *lexer)
{
    for (;;) {
        const char *cursor = lexer->current;
        TokenKind kind;
/*!re2c
            re2c:api = generic;
            re2c:api:style = free-form;
            re2c:YYCTYPE = "unsigned char";
            re2c:YYPEEK = "cursor < lexer->end ? (unsigned char)*cursor : 0";
            re2c:YYSKIP = "++cursor;";
            re2c:YYLESSTHAN = "cursor >= lexer->end";
            re2c:yyfill:enable = 0;
            re2c:eof = 0;

            digit = [0-9];
            identifier = [a-zA-Z_][a-zA-Z_0-9]*;
            number_tail = [a-zA-Z_0-9.]*;
            text_body = [^"\r\n\x00]*;

            [ \t\r\n\v\f]+ { finish(lexer, cursor, TOK_INVALID); continue; }
            $ { return finish(lexer, cursor, TOK_EOF); }
            identifier { kind = keyword_kind(lexer->current, (size_t)(cursor - lexer->current)); goto matched; }
            digit+ "." digit+ { kind = TOK_NUM_DEC; goto matched; }
            digit+ { kind = TOK_NUM_INT; goto matched; }
            /* Equal-length valid numbers win above. Longer malformed spans win here. */
            digit number_tail | "." number_tail { kind = TOK_INVALID; goto matched; }
            "\"" text_body "\"" { kind = TOK_TEXTO; goto matched; }
            "\"" text_body { kind = TOK_INVALID; goto matched; }
            "//" [^\r\n\x00]* ("\r\n" | [\r\n])? { kind = TOK_COMMENT; goto matched; }
            "==" | "!=" | "<=" | ">=" | "<" | ">" { kind = TOK_COMP; goto matched; }
            "&&" { kind = TOK_AND; goto matched; }
            "||" { kind = TOK_OR; goto matched; }
            "=" { kind = TOK_ASSIGN; goto matched; }
            "+" { kind = TOK_PLUS; goto matched; }
            "-" { kind = TOK_MINUS; goto matched; }
            "*" { kind = TOK_STAR; goto matched; }
            "/" { kind = TOK_SLASH; goto matched; }
            "%" { kind = TOK_PERCENT; goto matched; }
            "!" { kind = TOK_NOT; goto matched; }
            "(" { kind = TOK_LPAREN; goto matched; }
            ")" { kind = TOK_RPAREN; goto matched; }
            "[" { kind = TOK_LBRACKET; goto matched; }
            "]" { kind = TOK_RBRACKET; goto matched; }
            "{" { kind = TOK_LBRACE; goto matched; }
            "}" { kind = TOK_RBRACE; goto matched; }
            "," { kind = TOK_COMMA; goto matched; }
            ";" { kind = TOK_SEMICOLON; goto matched; }
            * { kind = TOK_INVALID; goto matched; }
*/
matched:
        return finish(lexer, cursor, kind);
    }
}
