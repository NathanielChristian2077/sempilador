#include "../parser/parser.h"
#include "../lexer/lexer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ParserFlow feed_source(ParserStream *p, const char *source)
{
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));
    for (;;) {
        Token token = lexer_next(&lexer);
        ParserStep step = parser_stream_feed(p, token);
        assert(step.consumed && step.flow != PARSER_PROTOCOL_ERROR);
        if (step.flow != PARSER_NEED_TOKEN) return step.flow;
    }
}

static void test_protocol(void)
{
    ParserStream *p = NULL;
    assert(parser_stream_create(&p, NULL) == PARSE_OK);
    Lexer lexer;
    lexer_init(&lexer, "main(){", 7);
    for (size_t i = 0; i < 4; ++i) {
        Token token = lexer_next(&lexer);
        assert(parser_stream_feed(p, token).flow == PARSER_NEED_TOKEN);
        assert(parser_stream_result(p)->status == PARSE_OK);
    }
    ParseResult result = parser_stream_finish(p);
    assert(result.status == PARSE_INVALID_INPUT && result.diagnostic_count == 1);
    assert(!result.diagnostics[0].has_found && result.diagnostics[0].token_index == 4);
    assert(result.diagnostics[0].expected[TOK_EOF]);
    assert(parser_stream_finish(p).diagnostic_count == 1);
    ParserStep step = parser_stream_feed(p, (Token){.kind = TOK_EOF});
    assert(!step.consumed && step.flow == PARSER_PROTOCOL_ERROR);
    parser_stream_destroy(p);

    assert(parser_stream_create(&p, NULL) == PARSE_OK);
    assert(feed_source(p, "main(){}") == PARSER_FINISHED);
    assert(parser_stream_finish(p).status == PARSE_OK);
    step = parser_stream_feed(p, (Token){.kind = TOK_EOF});
    assert(!step.consumed && step.flow == PARSER_PROTOCOL_ERROR);
    assert(parser_stream_finish(p).status == PARSE_INVALID_INPUT);
    step = parser_stream_feed(p, (Token){.kind = TOK_MAIN});
    assert(!step.consumed && step.flow == PARSER_PROTOCOL_ERROR);
    assert(parser_stream_finish(p).diagnostic_count == 1);
    parser_stream_destroy(p);

    assert(parser_stream_create(&p, NULL) == PARSE_OK);
    assert(feed_source(p, "main(){") == PARSER_FINISHED);
    result = parser_stream_finish(p);
    assert(result.status == PARSE_SYNTAX_ERROR && result.diagnostic_count == 1);
    assert(result.diagnostics[0].found == TOK_EOF && result.diagnostics[0].expected[TOK_RBRACE]);
    parser_stream_destroy(p);

    assert(parser_stream_create(&p, NULL) == PARSE_OK);
    step = parser_stream_feed(p, (Token){.kind = (TokenKind)-1});
    assert(step.consumed && step.flow == PARSER_STOPPED);
    assert(parser_stream_finish(p).status == PARSE_INVALID_INPUT);
    parser_stream_destroy(p);
    assert(parser_stream_create(NULL, NULL) == PARSE_INVALID_INPUT);
    assert(parser_stream_result(NULL) == NULL);
    assert(parser_stream_finish(NULL).status == PARSE_INVALID_INPUT);
    assert(parser_stream_feed(NULL, (Token){0}).flow == PARSER_PROTOCOL_ERROR);
    parser_stream_destroy(NULL);
}

static void test_interleaved(void)
{
    const char *sources[] = {"void f(){} main(){x=1;}", "main(){x=1 y//note\n=; z=;}"};
    ParserStream *parsers[2] = {0};
    Lexer lexers[2];
    bool done[2] = {false, false};
    for (size_t i = 0; i < 2; ++i) {
        assert(parser_stream_create(&parsers[i], NULL) == PARSE_OK);
        lexer_init(&lexers[i], sources[i], strlen(sources[i]));
    }
    while (!done[0] || !done[1]) {
        for (size_t i = 0; i < 2; ++i) {
            if (done[i]) continue;
            Token token = lexer_next(&lexers[i]);
            ParserStep step = parser_stream_feed(parsers[i], token);
            assert(step.consumed);
            done[i] = step.flow != PARSER_NEED_TOKEN;
            memset(&token, 0, sizeof(token));
        }
    }
    assert(parser_stream_finish(parsers[0]).status == PARSE_OK);
    ParseResult result = parser_stream_finish(parsers[1]);
    assert(result.status == PARSE_SYNTAX_ERROR && result.diagnostic_count == 3);
    assert(result.diagnostics[0].expected[TOK_SEMICOLON]);
    assert(result.diagnostics[1].line == 2 && result.diagnostics[1].column == 2);
    for (size_t i = 0; i < 2; ++i) parser_stream_destroy(parsers[i]);
}

typedef struct { size_t attempts, fail_at, live; } AllocationTest;

static void *test_allocate(void *context, size_t bytes)
{
    AllocationTest *test = context;
    if (test->attempts++ == test->fail_at) return NULL;
    void *memory = malloc(bytes);
    assert(memory != NULL);
    ++test->live;
    return memory;
}

static void test_release(void *context, void *memory)
{
    AllocationTest *test = context;
    if (memory == NULL) return;
    assert(test->live > 0);
    --test->live;
    free(memory);
}

static ParseStatus allocation_scenario(AllocationTest *test)
{
    ParserConfig config = {.allocator = {test, test_allocate, test_release}};
    ParserStream *p = NULL;
    ParseStatus status = parser_stream_create(&p, &config);
    if (status != PARSE_OK) { assert(p == NULL); return status; }
    char source[2048] = "main(){";
    for (size_t i = 0; i < 100; ++i) strcat(source, "while(1){");
    for (size_t i = 0; i < 101; ++i) strcat(source, "}");
    feed_source(p, source);
    status = parser_stream_finish(p).status;
    parser_stream_destroy(p);
    return status;
}

static void test_resources(void)
{
    AllocationTest baseline = {.fail_at = SIZE_MAX};
    assert(allocation_scenario(&baseline) == PARSE_OK && baseline.live == 0);
    assert(baseline.attempts > 3);
    for (size_t i = 0; i < baseline.attempts; ++i) {
        AllocationTest failure = {.fail_at = i};
        assert(allocation_scenario(&failure) == PARSE_NO_MEMORY);
        assert(failure.live == 0);
    }
    ParserStream *p = NULL;
    ParserConfig config = {.max_stack = 4};
    assert(parser_stream_create(&p, &config) == PARSE_OK);
    assert(feed_source(p, "main(){}") == PARSER_STOPPED);
    assert(parser_stream_finish(p).status == PARSE_LIMIT_EXCEEDED);
    parser_stream_destroy(p);
    config.max_stack = 1;
    assert(parser_stream_create(&p, &config) == PARSE_LIMIT_EXCEEDED && p == NULL);
    config = (ParserConfig){.allocator = {NULL, test_allocate, NULL}};
    assert(parser_stream_create(&p, &config) == PARSE_INVALID_INPUT && p == NULL);
    config = (ParserConfig){.max_stack = SIZE_MAX};
    assert(parser_stream_create(&p, &config) == PARSE_INVALID_INPUT && p == NULL);
}

static void test_depth_and_long_lists(void)
{
    for (size_t depth = 10000; depth <= 20000; depth += 10000) {
        char *source = malloc(2 * depth + 32);
        assert(source != NULL);
        char *out = source;
        memcpy(out, "main(){x=", 9); out += 9;
        memset(out, '(', depth); out += depth;
        *out++ = '1';
        memset(out, ')', depth); out += depth;
        memcpy(out, ";}", 3);
        ParserStream *p = NULL;
        assert(parser_stream_create(&p, NULL) == PARSE_OK);
        feed_source(p, source);
        assert(parser_stream_finish(p).status == (depth == 10000 ? PARSE_OK : PARSE_LIMIT_EXCEEDED));
        parser_stream_destroy(p);
        free(source);
    }
    ParserStream *p = NULL;
    assert(parser_stream_create(&p, NULL) == PARSE_OK);
    TokenKind header[] = {TOK_MAIN, TOK_LPAREN, TOK_RPAREN, TOK_LBRACE};
    for (size_t i = 0; i < 4; ++i) parser_stream_feed(p, (Token){.kind = header[i]});
    TokenKind command[] = {TOK_ID, TOK_ASSIGN, TOK_NUM_INT, TOK_SEMICOLON};
    for (size_t n = 0; n < 50000; ++n)
        for (size_t i = 0; i < 4; ++i)
            assert(parser_stream_feed(p, (Token){.kind = command[i]}).flow == PARSER_NEED_TOKEN);
    parser_stream_feed(p, (Token){.kind = TOK_RBRACE});
    assert(parser_stream_feed(p, (Token){.kind = TOK_EOF}).flow == PARSER_FINISHED);
    assert(parser_stream_finish(p).status == PARSE_OK);
    parser_stream_destroy(p);
}

int main(void)
{
    test_protocol();
    test_interleaved();
    test_resources();
    test_depth_and_long_lists();
    puts("parser_stream_test: OK (protocol, isolation, recovery, OOM and depth)");
    return 0;
}
