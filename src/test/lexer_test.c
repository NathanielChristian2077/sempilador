#include "../lexer/lexer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(
    const char *source,
    const TokenKind *expected,
    size_t count
) {
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));

    for (size_t i = 0; i < count; ++i) {
        Token token = lexer_next(&lexer);

        assert(token.kind == expected[i]);
    }

    assert(lexer_next(&lexer).kind == TOK_EOF);
}

int main(void)
{
    const char source[] =
        "int main() {\r\n"
        "  int x = 42; // comentario\r\n"
        "  if (x >= 10 && x != 0) {\r\n"
        "    println(\"ok\");\r\n"
        "  }\r\n"
        "}";

    const TokenKind expected[] = {
        TOK_INT, TOK_MAIN, TOK_LPAREN,
        TOK_RPAREN, TOK_LBRACE,

        TOK_INT, TOK_ID, TOK_ASSIGN,
        TOK_NUM_INT, TOK_SEMICOLON,
        TOK_COMMENT,

        TOK_IF, TOK_LPAREN, TOK_ID,
        TOK_COMP, TOK_NUM_INT, TOK_AND,
        TOK_ID, TOK_COMP, TOK_NUM_INT,
        TOK_RPAREN, TOK_LBRACE,

        TOK_PRINTLN, TOK_LPAREN,
        TOK_TEXTO, TOK_RPAREN,
        TOK_SEMICOLON,

        TOK_RBRACE, TOK_RBRACE
    };

    check(
        source,
        expected,
        sizeof(expected) / sizeof(expected[0])
    );

    Lexer lexer;
    lexer_init(&lexer, source, sizeof(source) - 1);

    Token token;
    do {
        token = lexer_next(&lexer);

        if (
            token.kind == TOK_COMP &&
            token.length == 2 &&
            memcmp(token.begin, ">=", 2) == 0
        ) {
            assert(token.line == 3);
            assert(token.column == 9);
        }

    } while (token.kind != TOK_EOF);

    assert(token.line == 6);
    assert(token.column == 2);

    const char edge[] =
        "if ifx func for 12 12.5 12. "
        "@ & | \"abc\" \"x\n";

    const TokenKind expected_edge[] = {
        TOK_IF,
        TOK_ID,
        TOK_FUNC,
        TOK_FOR,
        TOK_NUM_INT,
        TOK_NUM_DEC,
        TOK_NUM_INT,
        TOK_INVALID,
        TOK_INVALID,
        TOK_INVALID,
        TOK_INVALID,
        TOK_TEXTO,
        TOK_INVALID
    };

    check(
        edge,
        expected_edge,
        sizeof(expected_edge) / sizeof(expected_edge[0])
    );

    puts("lexer_test: OK");
    return 0;
}
