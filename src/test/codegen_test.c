#include "../codegen/codegen.h"
#include "../lexer/lexer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static Ast *parse(const char *source)
{
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));
    ParserStream *p = NULL;
    assert(parser_stream_create_ast(&p, NULL, 0) == PARSE_OK);
    while (parser_stream_feed(p, lexer_next(&lexer)).flow == PARSER_NEED_TOKEN)
        ;
    Ast *ast = parser_stream_take_ast(p);
    assert(ast != NULL);
    parser_stream_destroy(p);
    return ast;
}

/* Run the whole pipeline and leave the emitted IR in text. */
static bool emit(const char *source, char *text, size_t capacity)
{
    Ast *ast = parse(source);
    SemanticModel *model = NULL;
    semantic_analyze(ast, NULL, &model);
    const char *path = "codegen_test.ll";
    FILE *stream = fopen(path, "w+b");
    assert(stream != NULL);
    bool ok = codegen_emit(ast, model, stream);
    rewind(stream);
    size_t length = fread(text, 1, capacity - 1, stream);
    assert(length < capacity - 1);
    text[length] = '\0';
    fclose(stream);
    remove(path);
    semantic_model_destroy(model);
    ast_destroy(ast);
    return ok;
}

int main(void)
{
    static char text[1 << 16];
    assert(!codegen_emit(NULL, NULL, stdout));

    /* A program rejected by the semantic pass produces no IR. */
    assert(!emit("main(){int x; x=1.5;}", text, sizeof(text)));
    assert(text[0] == '\0');

    assert(emit("main(){}", text, sizeof(text)));
    assert(strstr(text, "define i32 @main() {\nentry:\n  ret i32 0\n}") != NULL);

    /* Folded constants go straight into the IR; doubles use exact hex. */
    assert(emit("main(){int x; float y; x=-2147483648; y=0.5+1;}", text, sizeof(text)));
    assert(strstr(text, "store i32 -2147483648, ptr") != NULL);
    assert(strstr(text, "store double 0x3FF8000000000000, ptr") != NULL);

    /* Mixed arithmetic converts only the int side. */
    assert(emit("main(){int i; float f; f=i*2.5;}", text, sizeof(text)));
    assert(strstr(text, "sitofp i32") != NULL && strstr(text, "fmul double") != NULL);

    /* Variables declared in a loop live in the entry block, zeroed per iteration. */
    assert(emit("main(){int i; for(i=0;i<2;i=i+1){int k; k=k+1;}}", text, sizeof(text)));
    char *alloca_k = strstr(text, "%v1 = alloca i32");
    char *loop = strstr(text, "for.body.1:");
    assert(alloca_k != NULL && loop != NULL && alloca_k < loop);
    assert(strstr(loop, "store i32 0, ptr %v1") != NULL);

    /* User functions are prefixed so they never clash with libc names. */
    assert(emit("void printf(){} main(){func printf()}", text, sizeof(text)));
    assert(strstr(text, "define void @f.printf()") != NULL);
    assert(strstr(text, "call void @f.printf()") != NULL);

    /* Arrays are returned through a hidden pointer and copied on entry. */
    assert(emit("int[2] f(int[2] a){return a;} main(){int[2] x; x=func f(x);}", text,
                sizeof(text)));
    assert(strstr(text, "define void @f.f(ptr %result, ptr %p0)") != NULL);
    assert(strstr(text, "memmove.p0.p0.i64(ptr %result") != NULL);

    /* Text bytes outside printable ASCII, quotes and backslashes are escaped. */
    assert(emit("main(){println(\"a\\b\xC3\xA7\");}", text, sizeof(text)));
    assert(strstr(text, "c\"a\\5Cb\\C3\\A7\\00\"") != NULL);

    /* Short-circuit: the right side lives in its own block. */
    assert(emit("main(){boolean a,b; a=a&&b;}", text, sizeof(text)));
    assert(strstr(text, ", label %and.rhs.1, label %and.end.1") != NULL);
    assert(strstr(text, "and.rhs.1:") != NULL && strstr(text, "and.end.1:") != NULL);

    printf("codegen_test: OK\n");
    return 0;
}
