#include "../lexer/lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    TokenKind kind;
    const char *lexeme;
    uint32_t line;
    uint32_t column;
} Expected;

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void check(
    const char *source,
    size_t length,
    const Expected *expected,
    size_t count
) {
    Lexer lexer;
    lexer_init(&lexer, source, length);

    for (size_t i = 0; i < count; ++i) {
        Token t = lexer_next(&lexer);
        const Expected *e = &expected[i];
        size_t len = strlen(e->lexeme);

        if (t.kind != e->kind ||
            t.length != len ||
            t.line != e->line ||
            t.column != e->column ||
            memcmp(t.begin, e->lexeme, len) != 0) {

            fprintf(stderr,
                "Falha no token %zu: "
                "kind=%d, linha=%u, coluna=%u\n",
                i, (int)t.kind, t.line, t.column);

            exit(EXIT_FAILURE);
        }
    }

    if (lexer_next(&lexer).kind != TOK_EOF) {
        fprintf(stderr, "EOF esperado\n");
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    const char keywords[] =
        "if ifelse integer boolean booleanx func for";

    const Expected kw[] = {
        {TOK_IF,      "if",       1, 1},
        {TOK_ID,      "ifelse",   1, 4},
        {TOK_ID,      "integer",  1, 11},
        {TOK_BOOLEAN, "boolean",  1, 19},
        {TOK_ID,      "booleanx", 1, 27},
        {TOK_FUNC,    "func",     1, 36},
        {TOK_FOR,     "for",      1, 41}
    };

    check(keywords, sizeof(keywords) - 1,
          kw, COUNT(kw));

    const char program[] =
        "int main(){\r\n"
        "  int x=12.5; //c\r\n"
        "}";

    const Expected tokens[] = {
        {TOK_INT,       "int",      1, 1},
        {TOK_MAIN,      "main",     1, 5},
        {TOK_LPAREN,    "(",        1, 9},
        {TOK_RPAREN,    ")",        1, 10},
        {TOK_LBRACE,    "{",        1, 11},
        {TOK_INT,       "int",      2, 3},
        {TOK_ID,        "x",        2, 7},
        {TOK_ASSIGN,    "=",        2, 8},
        {TOK_NUM_DEC,   "12.5",     2, 9},
        {TOK_SEMICOLON, ";",        2, 13},
        {TOK_COMMENT,   "//c\r\n",  2, 15},
        {TOK_RBRACE,    "}",        3, 1}
    };

    check(program, sizeof(program) - 1,
          tokens, COUNT(tokens));

    const char invalid[] =
        "123. @ & | \"x\" \"z\n";

    const Expected bad[] = {
        {TOK_NUM_INT, "123", 1, 1},
        {TOK_INVALID, ".",   1, 4},
        {TOK_INVALID, "@",   1, 6},
        {TOK_INVALID, "&",   1, 8},
        {TOK_INVALID, "|",   1, 10},
        {TOK_TEXTO,   "\"x\"", 1, 12},
        {TOK_INVALID, "\"z", 1, 16}
    };

    check(invalid, sizeof(invalid) - 1,
          bad, COUNT(bad));

    check("", 0, NULL, 0);

    puts("lexer_regression_test: OK");
    return 0;
}
