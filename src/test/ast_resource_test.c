#include "../parser/ast.h"
#include "../lexer/lexer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t attempts, fail_at, live, bytes, peak;
} Memory;
typedef union {
    max_align_t align;
    size_t bytes;
} Header;
static void *allocate(void *context, size_t bytes)
{
    Memory *m = context;
    if (m->attempts++ == m->fail_at)
        return NULL;
    Header *h = malloc(sizeof(*h) + bytes);
    assert(h);
    h->bytes = bytes;
    ++m->live;
    m->bytes += bytes;
    if (m->bytes > m->peak)
        m->peak = m->bytes;
    return h + 1;
}
static void release(void *context, void *memory)
{
    Memory *m = context;
    assert(memory && m->live);
    Header *h = (Header *)memory - 1;
    --m->live;
    m->bytes -= h->bytes;
    free(h);
}
static ParseStatus run(const char *source, Memory *m, size_t limit)
{
    ParserStream *p = NULL;
    ParserConfig config = {.max_stack = limit, .allocator = {m, allocate, release}};
    ParseStatus status = parser_stream_create_ast(&p, &config, 3);
    if (status != PARSE_OK) {
        assert(!p && !m->live);
        return status;
    }
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));
    for (;;) {
        Token t = lexer_next(&lexer);
        ParserStep s = parser_stream_feed(p, t);
        assert(s.consumed);
        if (s.flow != PARSER_NEED_TOKEN)
            break;
    }
    parser_stream_finish(p);
    Ast *ast = parser_stream_take_ast(p);
    status = parser_stream_result(p)->status;
    assert((ast != NULL) == (status == PARSE_OK));
    parser_stream_destroy(p);
    if (ast) {
        assert(ast_node(ast, ast_root(ast))->kind == AST_PROGRAM);
        ast_destroy(ast);
    }
    assert(!m->live && !m->bytes);
    return status;
}
static char *program(size_t statements, size_t nesting)
{
    size_t capacity = 100 + statements * 8 + nesting * 2;
    char *source = malloc(capacity);
    assert(source);
    char *p = source;
    const char *start = "int[2][3] f(int[2][3] a,float b){return b;} main(){int[2][3] x,y;";
    size_t length = strlen(start);
    memcpy(p, start, length);
    p += length;
    for (size_t i = 0; i < statements; ++i) {
        memcpy(p, "x=x+1;", 6);
        p += 6;
    }
    if (nesting) {
        memcpy(p, "x=", 2);
        p += 2;
        memset(p, '(', nesting);
        p += nesting;
        *p++ = '1';
        memset(p, ')', nesting);
        p += nesting;
        *p++ = ';';
    }
    *p++ = '}';
    *p = '\0';
    assert((size_t)(p - source) < capacity);
    return source;
}
static void failures(void)
{
    char *source = program(70, 90);
    Memory baseline = {.fail_at = SIZE_MAX};
    assert(run(source, &baseline, 0) == PARSE_OK);
    size_t total = baseline.attempts;
    assert(total > 10);
    for (size_t i = 0; i < total; ++i) {
        Memory m = {.fail_at = i};
        assert(run(source, &m, 0) == PARSE_NO_MEMORY);
    }
    /* Every possible interruption point releases frames, attributes and tree. */
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));
    size_t count = 0;
    for (;;) {
        Token t = lexer_next(&lexer);
        ++count;
        if (t.kind == TOK_EOF)
            break;
    }
    for (size_t cut = 0; cut < count; ++cut) {
        Memory m = {.fail_at = SIZE_MAX};
        ParserConfig config = {.allocator = {&m, allocate, release}};
        ParserStream *p = NULL;
        assert(parser_stream_create_ast(&p, &config, 0) == PARSE_OK);
        lexer_init(&lexer, source, strlen(source));
        for (size_t i = 0; i < cut; ++i)
            assert(parser_stream_feed(p, lexer_next(&lexer)).flow == PARSER_NEED_TOKEN);
        parser_stream_destroy(p);
        assert(!m.live && !m.bytes);
    }
    printf("ast_resource_test: injected %zu allocation failures and %zu interruptions\n", total,
           count);
    free(source);
}
static void limits_and_growth(void)
{
    char *source = program(5000, 1000);
    Memory m = {.fail_at = SIZE_MAX};
    assert(run(source, &m, 0) == PARSE_OK);
    printf("ast_resource_test: 5000 statements + 1000 parentheses, peak %zu bytes\n", m.peak);
    m = (Memory){.fail_at = SIZE_MAX};
    assert(run(source, &m, 32) == PARSE_LIMIT_EXCEEDED);
    free(source);
    source = program(0, 20000);
    m = (Memory){.fail_at = SIZE_MAX};
    assert(run(source, &m, 0) == PARSE_LIMIT_EXCEEDED);
    free(source);
    /* A syntax error after allocations abandons the tree, preserves recovery. */
    m = (Memory){.fail_at = SIZE_MAX};
    assert(run("main(){int[2] a,b; x=1; y=; z=2;}", &m, 0) == PARSE_SYNTAX_ERROR);
    m = (Memory){.fail_at = SIZE_MAX};
    assert(run("main(){x=1; @}", &m, 0) == PARSE_LEXICAL_ERROR);
    /* Failed publication (last allocation) and transfer are covered above. */
}
static void independent_instances(void)
{
    const char *sources[] = {"main(){x=1+2;}", "main(){x=3*4;}"};
    Lexer lexer[2];
    ParserStream *p[2] = {0};
    bool done[2] = {0};
    for (size_t i = 0; i < 2; ++i) {
        assert(parser_stream_create_ast(&p[i], NULL, (uint32_t)i) == PARSE_OK);
        lexer_init(&lexer[i], sources[i], strlen(sources[i]));
    }
    while (!done[0] || !done[1])
        for (size_t i = 0; i < 2; ++i)
            if (!done[i]) {
                ParserStep s = parser_stream_feed(p[i], lexer_next(&lexer[i]));
                done[i] = s.flow != PARSER_NEED_TOKEN;
            }
    for (size_t i = 0; i < 2; ++i) {
        Ast *a = parser_stream_take_ast(p[i]);
        assert(a && ast_node(a, ast_root(a))->span.source == i);
        parser_stream_destroy(p[i]);
        ast_destroy(a);
    }
}
int main(void)
{
    failures();
    limits_and_growth();
    independent_instances();
    puts("ast_resource_test: OK (OOM, cancellation, limits, relocation, ownership, isolation)");
}
