#include "../parser/parser.h"
#include "../lexer/lexer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static ParseResult diagnose(const char *source)
{
    Token tokens[256];
    size_t count = 0;
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));
    do {
        assert(count < COUNT(tokens));
        tokens[count++] = lexer_next(&lexer);
    } while (tokens[count - 1].kind != TOK_EOF);
    ParseResult result = parser_parse(tokens, count);
    if (result.status != PARSE_OK) {
        assert(result.diagnostic.token_index < count);
        const Token *found = &tokens[result.diagnostic.token_index];
        assert(result.diagnostic.found == found->kind);
        assert(result.diagnostic.line == found->line);
        assert(result.diagnostic.column == found->column);
    }
    return result;
}

static void check_expected(ParseResult result, const TokenKind *kinds, size_t count)
{
    for (size_t i = 0; i <= TOK_COMMENT; ++i) {
        bool expected = false;
        for (size_t j = 0; j < count; ++j)
            expected = expected || kinds[j] == (TokenKind)i;
        assert(result.diagnostic.expected[i] == expected);
    }
}

#define EXPECT(result, ...) \
    check_expected(result, (const TokenKind[]){__VA_ARGS__}, \
                   COUNT(((const TokenKind[]){__VA_ARGS__})))

static ParseResult syntax(const char *source, uint32_t line, uint32_t column,
                          TokenKind found)
{
    ParseResult result = diagnose(source);
    assert(result.status == PARSE_SYNTAX_ERROR);
    assert(result.diagnostic.has_found);
    assert(result.diagnostic.has_location);
    assert(result.diagnostic.line == line);
    assert(result.diagnostic.column == column);
    assert(result.diagnostic.found == found);
    return result;
}

static void test_syntax(void)
{
    ParseResult result = syntax("main(){int x }", 1, 14, TOK_RBRACE);
    EXPECT(result, TOK_SEMICOLON);
    result = syntax("main() {\n  int x\n}", 3, 1, TOK_RBRACE);
    EXPECT(result, TOK_SEMICOLON);
    result = syntax("main(){int x // note\n}", 2, 1, TOK_RBRACE);
    EXPECT(result, TOK_SEMICOLON);
    result = syntax("main(){x=;}", 1, 10, TOK_SEMICOLON);
    EXPECT(result, TOK_ID, TOK_NUM_INT, TOK_NUM_DEC, TOK_PLUS, TOK_MINUS,
           TOK_TEXTO, TOK_NOT, TOK_LPAREN, TOK_FUNC);
    result = syntax("main(){x=1+;}", 1, 12, TOK_SEMICOLON);
    EXPECT(result, TOK_ID, TOK_NUM_INT, TOK_NUM_DEC, TOK_PLUS, TOK_MINUS,
           TOK_TEXTO, TOK_NOT, TOK_LPAREN);
    result = syntax("main(){return;}", 1, 14, TOK_SEMICOLON);
    EXPECT(result, TOK_ID, TOK_NUM_INT, TOK_NUM_DEC, TOK_PLUS, TOK_MINUS,
           TOK_TEXTO, TOK_NOT, TOK_LPAREN);
    result = syntax("main(){if(){}}", 1, 11, TOK_RPAREN);
    EXPECT(result, TOK_ID, TOK_NUM_INT, TOK_NUM_DEC, TOK_PLUS, TOK_MINUS,
           TOK_TEXTO, TOK_NOT, TOK_LPAREN);
    result = syntax("main(){println();}", 1, 16, TOK_RPAREN);
    EXPECT(result, TOK_ID, TOK_NUM_INT, TOK_NUM_DEC, TOK_TEXTO);
    result = syntax("main(){scanf();}", 1, 14, TOK_RPAREN);
    EXPECT(result, TOK_ID);
    result = syntax("void f(int x,){} main(){}", 1, 14, TOK_RPAREN);
    EXPECT(result, TOK_CHAR, TOK_FLOAT, TOK_INT, TOK_BOOLEAN);
    result = syntax("main(){", 1, 8, TOK_EOF);
    EXPECT(result, TOK_RBRACE);
    result = syntax("main(){\n// end", 2, 7, TOK_EOF);
    EXPECT(result, TOK_RBRACE);
    result = syntax("main(){} x", 1, 10, TOK_ID);
    EXPECT(result, TOK_EOF);
    result = syntax("main(){;}", 1, 8, TOK_SEMICOLON);
    EXPECT(result, TOK_SCANF, TOK_PRINTLN, TOK_ID, TOK_FUNC, TOK_IF,
           TOK_WHILE, TOK_FOR, TOK_RETURN, TOK_RBRACE);
    result = syntax("main(){x=-;}", 1, 11, TOK_SEMICOLON);
    EXPECT(result, TOK_ID, TOK_NUM_INT, TOK_NUM_DEC);
    result = syntax("main(){int x } @", 1, 14, TOK_RBRACE);
    EXPECT(result, TOK_SEMICOLON);
    result = syntax("main(){x=; y=;}", 1, 10, TOK_SEMICOLON);
    EXPECT(result, TOK_ID, TOK_NUM_INT, TOK_NUM_DEC, TOK_PLUS, TOK_MINUS,
           TOK_TEXTO, TOK_NOT, TOK_LPAREN, TOK_FUNC);
}

static void test_valid_and_lexical(void)
{
    const char *valid[] = {
        "main(){}", "main(){x=2.5; println(3.5);}",
        "float f(){return 1.5;} main(){x=func f();}",
        "main(){for(;;){} if(1){}else{}}"
    };
    for (size_t i = 0; i < COUNT(valid); ++i) {
        ParseResult result = diagnose(valid[i]);
        assert(result.status == PARSE_OK);
        assert(!result.diagnostic.has_found);
        assert(!result.diagnostic.has_location);
        check_expected(result, NULL, 0);
    }
    ParseResult result = diagnose("main(){@}");
    assert(result.status == PARSE_LEXICAL_ERROR);
    assert(result.diagnostic.found == TOK_INVALID);
    assert(result.diagnostic.line == 1 && result.diagnostic.column == 8);
    check_expected(result, NULL, 0);
    result = diagnose("main(){x=\"abc\n}");
    assert(result.status == PARSE_LEXICAL_ERROR);
    assert(result.diagnostic.line == 1 && result.diagnostic.column == 10);
}

static void test_contract_and_preservation(void)
{
    ParseResult result = parser_parse(NULL, 0);
    assert(result.status == PARSE_INVALID_INPUT);
    assert(!result.diagnostic.has_found && !result.diagnostic.has_location);
    const Token missing_eof[] = {{.kind = TOK_MAIN, .line = 1, .column = 1}};
    result = parser_parse(missing_eof, COUNT(missing_eof));
    assert(result.status == PARSE_INVALID_INPUT);
    assert(result.diagnostic.token_index == COUNT(missing_eof));
    assert(!result.diagnostic.has_found && !result.diagnostic.has_location);
    EXPECT(result, TOK_EOF);
    const Token early_eof[] = {{.kind = TOK_EOF}, {.kind = TOK_EOF}};
    result = parser_parse(early_eof, COUNT(early_eof));
    assert(result.status == PARSE_INVALID_INPUT);
    assert(result.diagnostic.has_found && !result.diagnostic.has_location);
    const Token unknown[] = {{.kind = (TokenKind)-1}, {.kind = TOK_EOF}};
    assert(parser_parse(unknown, COUNT(unknown)).status == PARSE_INVALID_INPUT);

    const Token tokens[] = {
        {.kind = TOK_MAIN, .line = 2, .column = 3}, {.kind = TOK_EOF}
    };
    Parser parser;
    assert(parser_init(&parser, tokens, COUNT(tokens)));
    assert(!parser_expect(&parser, TOK_INT));
    assert(!parser_expect(&parser, TOK_FLOAT));
    assert(parser.current == 0);
    EXPECT(parser.result, TOK_INT);
    assert(parser.result.diagnostic.found == TOK_MAIN);
    assert(parser.result.diagnostic.line == 2);
    assert(parser.result.diagnostic.column == 3);
    assert(parser_init(&parser, tokens, COUNT(tokens)));
    assert(parser.result.status == PARSE_OK);
    assert(parser_expect(&parser, TOK_MAIN));
    assert(parser_init(&parser, tokens, COUNT(tokens)));
    assert(!parser_expect(&parser, (TokenKind)999));
    assert(parser.result.status == PARSE_INVALID_INPUT);
}

int main(void)
{
    test_syntax();
    test_valid_and_lexical();
    test_contract_and_preservation();
    puts("parser_diagnostic_test: OK");
    return 0;
}
