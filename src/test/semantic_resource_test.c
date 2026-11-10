#include "../semantic/semantic_model.h"
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
    assert(memory);
    Memory *m = context;
    assert(m->live);
    Header *h = (Header *)memory - 1;
    --m->live;
    m->bytes -= h->bytes;
    free(h);
}
static Ast *parse(const char *source)
{
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));
    ParserStream *p = NULL;
    assert(parser_stream_create_ast(&p, NULL, 1) == PARSE_OK);
    for (;;) {
        ParserStep s = parser_stream_feed(p, lexer_next(&lexer));
        if (s.flow != PARSER_NEED_TOKEN)
            break;
    }
    Ast *a = parser_stream_take_ast(p);
    assert(a);
    parser_stream_destroy(p);
    return a;
}
static SemanticStatus analyze(const Ast *a, Memory *memory)
{
    SemanticModel *m = NULL;
    ParserAllocator mem = {memory, allocate, release};
    SemanticResult r = semantic_analyze(a, &mem, &m);
    if (r.status == SEMANTIC_OK) {
        assert(m && semantic_model_valid(m));
        for (size_t i = 0; i < ast_count(a); ++i)
            assert(semantic_model_info(m, i)->state == SEM_VALID);
    } else {
        assert(!semantic_model_valid(m));
        if (m && (r.status == SEMANTIC_NO_MEMORY || r.status == SEMANTIC_LIMIT_EXCEEDED)) {
            assert(!r.complete && r.diagnostic_count);
            assert(r.diagnostics[r.diagnostic_count - 1].code == (r.status == SEMANTIC_NO_MEMORY
                                                                      ? SEM_DIAG_RESOURCE_MEMORY
                                                                      : SEM_DIAG_RESOURCE_LIMIT));
        }
    }
    semantic_model_destroy(m);
    assert(!memory->live && !memory->bytes);
    return r.status;
}
static void allocation_failures(void)
{
    char source[12000] =
        "int[2][3] f(int[2][3] a,int[3] b,float c){int[2][3] x;x=a;return x;} main(){";
    for (size_t i = 0; i < 80; ++i) {
        char decl[40];
        snprintf(decl, sizeof(decl), "int[2][3] a%zu;", i);
        strcat(source, decl);
    }
    for (size_t i = 0; i < 24; ++i)
        strcat(source, "if(1<2){int[2] q;scanf(q[0]);");
    for (size_t i = 0; i < 24; ++i)
        strcat(source, "}");
    strcat(source, "a0=func f(a1,a2[0],1.5);}");
    Ast *a = parse(source);
    Memory baseline = {.fail_at = SIZE_MAX};
    assert(analyze(a, &baseline) == SEMANTIC_OK);
    assert(baseline.attempts > 100);
    for (size_t fail = 0; fail < baseline.attempts; ++fail) {
        Memory m = {.fail_at = fail};
        SemanticStatus status = analyze(a, &m);
        if (status != SEMANTIC_NO_MEMORY)
            fprintf(stderr, "Failure point %zu returned %d\n", fail, status);
        assert(status == SEMANTIC_NO_MEMORY);
    }
    printf("semantic_resource_test: %zu allocation failures, all released\n", baseline.attempts);
    ast_destroy(a);
    const char *invalid[] = {
        "main(){int[0] x;x=x;}", "void f(int[2] p,int[0] q){} main(){func f(x,y)}",
        "int[2] f(){int[2] a;return a;} int[2] f(){int[2] a;return a;} main(){}"};
    for (size_t j = 0; j < 3; ++j) {
        a = parse(invalid[j]);
        baseline = (Memory){.fail_at = SIZE_MAX};
        assert(analyze(a, &baseline) == SEMANTIC_ERRORS);
        for (size_t fail = 0; fail < baseline.attempts; ++fail) {
            Memory m = {.fail_at = fail};
            assert(analyze(a, &m) == SEMANTIC_NO_MEMORY);
        }
        ast_destroy(a);
    }
}
static void limits_and_growth(void)
{
    char *source = malloc(60000);
    assert(source);
    strcpy(source, "main(){int x;");
    for (size_t i = 0; i < 2000; ++i)
        strcat(source, "x=x+1;");
    strcat(source, "}");
    Ast *a = parse(source);
    Memory mem = {.fail_at = SIZE_MAX};
    assert(analyze(a, &mem) == SEMANTIC_OK);
    printf("semantic_resource_test: 2000 assignments, peak %zu bytes\n", mem.peak);
    ast_destroy(a);
    strcpy(source, "main(){int x;x=");
    for (size_t i = 0; i < 300; ++i)
        strcat(source, "1+");
    strcat(source, "1;}");
    a = parse(source);
    mem = (Memory){.fail_at = SIZE_MAX};
    assert(analyze(a, &mem) == SEMANTIC_LIMIT_EXCEEDED);
    ast_destroy(a);
    strcpy(source, "main(){");
    for (size_t i = 0; i < 150; ++i)
        strcat(source, "if(1<2){");
    for (size_t i = 0; i < 151; ++i)
        strcat(source, "}");
    a = parse(source);
    mem = (Memory){.fail_at = SIZE_MAX};
    assert(analyze(a, &mem) == SEMANTIC_LIMIT_EXCEEDED);
    ast_destroy(a);
    free(source);
}
static void ownership_and_isolation(void)
{
    char source[16000] = "int[2][3] f(int[2][3] p){return p;} main(){";
    for (size_t i = 0; i < 500; ++i) {
        char decl[40];
        snprintf(decl, sizeof(decl), "int[2][3] a%zu;", i);
        strcat(source, decl);
    }
    strcat(source, "a499=func f(a0);}");
    Ast *a = parse(source);
    SemanticModel *models[2] = {0};
    for (size_t j = 0; j < 2; ++j)
        assert(semantic_analyze(a, NULL, &models[j]).status == SEMANTIC_OK);
    for (size_t i = 0; i < ast_count(a); ++i) {
        const SemanticInfo *row = semantic_model_info(models[0], i);
        if (row->symbol != SYMBOL_ID_NONE) {
            const SemanticSymbol *s =
                semantic_symbol(semantic_model_symbols(models[0]), row->symbol);
            assert(s);
            assert(row->type.concrete.dimensions == s->type.dimensions);
            assert(row->type.concrete.dimensions[0] == 2 && row->type.concrete.dimensions[1] == 3);
        }
    }
    semantic_model_destroy(models[0]);
    assert(semantic_model_valid(models[1]));
    semantic_model_destroy(models[1]);
    ast_destroy(a);
    Memory m = {.fail_at = SIZE_MAX};
    ParserAllocator bad = {&m, allocate, NULL};
    a = parse("main(){}");
    SemanticModel *model = (SemanticModel *)1;
    assert(semantic_analyze(a, &bad, &model).status == SEMANTIC_INVALID_INPUT && !model &&
           m.attempts == 0);
    ast_destroy(a);
}
int main(void)
{
    allocation_failures();
    limits_and_growth();
    ownership_and_isolation();
    puts("semantic_resource_test: OOM, depth, growth, dimensions, isolation OK");
}
