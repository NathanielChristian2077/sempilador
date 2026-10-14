#include "../parser/parser.h"

#include <assert.h>
#include <stdio.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void test_cursor(void)
{
    const Token tokens[] = {
        { .kind = TOK_COMMENT }, { .kind = TOK_MAIN },
        { .kind = TOK_LPAREN }, { .kind = TOK_RPAREN },
        { .kind = TOK_COMMENT }, { .kind = TOK_EOF }
    };
    Parser parser;
    assert(parser_init(&parser, tokens, COUNT(tokens)));
    assert(parser.current == 0);
    assert(parser_peek(&parser) == &tokens[1]);
    assert(parser.current == 1);
    assert(parser_peek(&parser) == &tokens[1]);
    assert(!parser_expect(&parser, TOK_INT));
    assert(parser.current == 1);
    assert(parser_expect(&parser, TOK_MAIN));
    assert(parser.current == 2);
    assert(parser_expect(&parser, TOK_LPAREN));
    assert(parser_expect(&parser, TOK_RPAREN));
    assert(parser_expect(&parser, TOK_EOF));
    assert(parser.current == 5);
    assert(parser_expect(&parser, TOK_EOF));
    assert(parser.current == 5);
    assert(parser_peek(NULL) == NULL);
    assert(!parser_expect(NULL, TOK_MAIN));
    assert(!parser_init(NULL, tokens, COUNT(tokens)));
    assert(!parser_init(&parser, NULL, COUNT(tokens)));
    assert(!parser_init(&parser, tokens, 0));

    const Token invalid[] = {{ .kind = TOK_INVALID }};
    assert(parser_init(&parser, invalid, COUNT(invalid)));
    assert(!parser_expect(&parser, TOK_INVALID));
    assert(parser.current == 0);
    const Token truncated[] = {{ .kind = TOK_MAIN }};
    assert(parser_init(&parser, truncated, COUNT(truncated)));
    assert(parser_expect(&parser, TOK_MAIN));
    assert(parser_peek(&parser) == NULL);
    assert(!parser_expect(&parser, TOK_EOF));
}

static void check_kinds(const TokenKind *kinds, size_t count, bool expected)
{
    Token tokens[256];
    assert(count <= COUNT(tokens));
    for (size_t i = 0; i < count; ++i)
        tokens[i] = (Token){ .kind = kinds[i], .line = 1, .column = (uint32_t)i + 1 };
    assert(parser_parse(tokens, count) == expected);
}

static void test_artificial_tokens(void)
{
    const TokenKind empty[] = {
        TOK_MAIN, TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE, TOK_EOF
    };
    check_kinds(empty, COUNT(empty), true);
    const TokenKind declarations[] = {
        TOK_COMMENT, TOK_MAIN, TOK_LPAREN, TOK_RPAREN, TOK_LBRACE,
        TOK_INT, TOK_LBRACKET, TOK_NUM_INT, TOK_RBRACKET,
        TOK_LBRACKET, TOK_NUM_INT, TOK_RBRACKET, TOK_ID, TOK_COMMA, TOK_ID,
        TOK_SEMICOLON, TOK_CHAR, TOK_ID, TOK_SEMICOLON,
        TOK_RBRACE, TOK_COMMENT, TOK_EOF
    };
    check_kinds(declarations, COUNT(declarations), true);
    
    Token prefix[COUNT(declarations)];
    for (size_t n = 0; n < 19; ++n) {
        for (size_t i = 0; i < n; ++i)
            prefix[i] = (Token){ .kind = declarations[i] };
        prefix[n] = (Token){ .kind = TOK_EOF };
        assert(!parser_parse(prefix, n + 1));
    }

    const TokenKind assignment[] = {
        TOK_MAIN, TOK_LPAREN, TOK_RPAREN, TOK_LBRACE,
        TOK_ID, TOK_ASSIGN, TOK_MINUS, TOK_NUM_INT, TOK_PLUS, TOK_NUM_DEC,
        TOK_STAR, TOK_LPAREN, TOK_ID, TOK_PERCENT, TOK_NUM_INT, TOK_RPAREN,
        TOK_SEMICOLON, TOK_RBRACE, TOK_EOF
    };
    check_kinds(assignment, COUNT(assignment), true);
    const TokenKind invalid[] = {
        TOK_MAIN, TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_INVALID,
        TOK_RBRACE, TOK_EOF
    };
    check_kinds(invalid, COUNT(invalid), false);
    const TokenKind early_eof[] = {TOK_EOF, TOK_MAIN, TOK_EOF};
    check_kinds(early_eof, COUNT(early_eof), false);
    const TokenKind no_eof[] = {
        TOK_MAIN, TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE
    };
    check_kinds(no_eof, COUNT(no_eof), false);
    const TokenKind unknown[] = {(TokenKind)999, TOK_EOF};
    check_kinds(unknown, COUNT(unknown), false);
    assert(!parser_parse(NULL, 0));
    const Token end = { .kind = TOK_EOF };
    assert(!parser_parse(&end, 1));

    Token main_tokens[COUNT(empty)];
    for (size_t i = 0; i < COUNT(empty); ++i)
        main_tokens[i] = (Token){ .kind = empty[i] };
    Parser parser;
    assert(parser_init(&parser, main_tokens, COUNT(main_tokens)));
    assert(parse_main(&parser));
    assert(parser.current == COUNT(main_tokens) - 1);
    assert(parser_expect(&parser, TOK_EOF));
    
    for (size_t i = 0; i < 20; ++i)
        assert(parser_parse(main_tokens, COUNT(main_tokens)));
}

int main(void)
{
    test_cursor();
    test_artificial_tokens();
    puts("parser_test: OK (cursor e tokens artificiais)");
    return 0;
}
