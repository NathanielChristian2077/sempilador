#include "../parser/parser.h"
#include "../lexer/lexer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static ParseResult parse(const char *source)
{
    Token tokens[4096];
    size_t count = 0;
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));
    do {
        assert(count < COUNT(tokens));
        tokens[count++] = lexer_next(&lexer);
    } while (tokens[count - 1].kind != TOK_EOF);
    ParseResult result = parser_parse(tokens, count);
    assert(result.diagnostic_count <= PARSE_MAX_DIAGNOSTICS);
    for (size_t i = 0; i < result.diagnostic_count; ++i) {
        const ParseDiagnostic *error = &result.diagnostics[i];
        assert(error->has_found && error->has_location);
        assert(error->token_index < count);
        assert(error->found == tokens[error->token_index].kind);
        assert(error->line == tokens[error->token_index].line);
        assert(error->column == tokens[error->token_index].column);
        if (i > 0)
            assert(result.diagnostics[i - 1].token_index <= error->token_index);
    }
    return result;
}

static ParseResult syntax(const char *name, const char *source, size_t errors)
{
    ParseResult result = parse(source);
    if (result.status != PARSE_SYNTAX_ERROR || result.diagnostic_count != errors) {
        fprintf(stderr, "%s: status=%d, errors=%zu, expected=%zu\n",
                name, (int)result.status, result.diagnostic_count, errors);
        for (size_t i = 0; i < result.diagnostic_count; ++i)
            fprintf(stderr, "  %u:%u token=%d\n", result.diagnostics[i].line,
                    result.diagnostics[i].column, (int)result.diagnostics[i].found);
        exit(EXIT_FAILURE);
    }
    for (size_t i = 0; i < errors; ++i) {
        assert(result.diagnostics[i].status == PARSE_SYNTAX_ERROR);
        bool expected = false;
        for (size_t j = 0; j <= TOK_COMMENT; ++j)
            expected = expected || result.diagnostics[i].expected[j];
        assert(expected);
    }
    return result;
}

static void test_recovery(void)
{
    ParseResult result = syntax("commands",
        "main(){\n x=;\n println(x);\n y=;\n}", 2);
    assert(result.diagnostics[0].line == 2);
    assert(result.diagnostics[1].line == 4);
    assert(result.diagnostics[0].column == 4);
    syntax("missing separator", "main(){x=1 println(); y=;}", 3);
    result = syntax("missing separator before assignment", "main(){x=1 y=;}", 2);
    assert(result.diagnostics[0].expected[TOK_SEMICOLON]);
    syntax("declarations", "main(){int x,; float y; int z,; println(1);x=;}", 3);
    syntax("broken dimension", "main(){int[; float y; z=;}", 2);
    syntax("missing declaration separator", "main(){int x float y; z=;}", 2);
    syntax("declaration after command", "main(){x=1; int y; z=;}", 2);
    syntax("nested lists", "main(){if(1){x=; y=;}else{z=;} w=;}", 4);
    syntax("damaged expression", "main(){x=(1+; y=; z=1;}", 2);
    syntax("damaged call", "main(){func f(1,; y=;}", 2);
    syntax("for header", "main(){for(i=;i<10;i=i+1){x=;} y=;}", 2);
    syntax("for missing parenthesis", "main(){for(i=;;{x=;} y=;}", 2);
    syntax("if header and else", "main(){if(){x=;}else{y=;} z=;}", 2);
    syntax("while header", "main(){while(){if(1){x=;}} y=;}", 2);
    syntax("function header",
        "int bad(int x,){x=;} void good(){y=;} main(){z=;}", 3);
    syntax("missing function brace", "void f(){x=; main(){y=;}", 3);
    syntax("top-level noise", "123; void f(){} main(){x=;}", 2);
    syntax("stray separators", "main(){;;;x=1; y=;}", 4);
    syntax("comments", "main(){x=1 //comment\n println(); //comment\n y=;}", 3);
    syntax("missing block close", "main(){x=;", 2);
    syntax("valid suffix inside same block", "main(){x=; y=1; println();}", 2);

    result = parse("main(){x=; @ y=;}");
    assert(result.status == PARSE_LEXICAL_ERROR);
    assert(result.diagnostic_count == 2);
    assert(result.diagnostics[0].status == PARSE_SYNTAX_ERROR);
    assert(result.diagnostics[1].status == PARSE_LEXICAL_ERROR);
    result = parse("main(){for(i=;@;){}} main(){}");
    assert(result.status == PARSE_LEXICAL_ERROR);
    assert(result.diagnostic_count == 2);
    result = parse("main(){int x; x=2.5; for(;;){if(x){}else{}}}");
    assert(result.status == PARSE_OK && result.diagnostic_count == 0);
    assert(!result.limit_reached);
}

static void test_limit(void)
{
    char source[512] = "main(){";
    for (size_t i = 0; i < PARSE_MAX_DIAGNOSTICS + 10; ++i)
        strcat(source, "x=;");
    strcat(source, "}");
    ParseResult result = syntax("error limit", source, PARSE_MAX_DIAGNOSTICS);
    assert(result.limit_reached);
    for (size_t i = 0; i < result.diagnostic_count; ++i) {
        assert(result.diagnostics[i].found == TOK_SEMICOLON);
        assert(result.diagnostics[i].expected[TOK_ID]);
    }
}

static void test_generated_inputs(void)
{
    uint32_t state = 12345;
    for (size_t sample = 0; sample < 2000; ++sample) {
        Token tokens[65];
        state = state * 1664525u + 1013904223u;
        size_t count = state % 64 + 1;
        for (size_t i = 0; i + 1 < count; ++i) {
            state = state * 1664525u + 1013904223u;
            TokenKind kind = (TokenKind)(TOK_ID + state % (TOK_COMMENT - TOK_ID + 1));
            tokens[i] = (Token){.kind = kind, .line = 1, .column = (uint32_t)i + 1};
        }
        tokens[count - 1] = (Token){.kind = TOK_EOF, .line = 1, .column = (uint32_t)count};
        ParseResult result = parser_parse(tokens, count);
        assert(result.diagnostic_count <= PARSE_MAX_DIAGNOSTICS);
        if (result.status != PARSE_OK)
            assert(result.diagnostic_count > 0);
    }
}

int main(void)
{
    test_recovery();
    test_limit();
    test_generated_inputs();
    puts("parser_recovery_test: OK");
    return 0;
}
