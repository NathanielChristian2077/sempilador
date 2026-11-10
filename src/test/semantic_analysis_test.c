#include "../semantic/semantic_model.h"
#include "../lexer/lexer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <locale.h>
#include <string.h>
static Ast *parse(const char *source)
{
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));
    ParserStream *p = NULL;
    assert(parser_stream_create_ast(&p, NULL, 7) == PARSE_OK);
    for (;;) {
        ParserStep step = parser_stream_feed(p, lexer_next(&lexer));
        if (step.flow != PARSER_NEED_TOKEN)
            break;
    }
    Ast *a = parser_stream_take_ast(p);
    if (!a)
        fprintf(stderr, "Syntax error: %s\n", source);
    assert(a);
    parser_stream_destroy(p);
    return a;
}
static size_t checks;
static void check(const char *source, int expected)
{
    ++checks;
    Ast *a = parse(source);
    SemanticModel *m = NULL;
    SemanticResult r = semantic_analyze(a, NULL, &m);
    if (expected < 0) {
        if (!semantic_model_valid(m))
            fprintf(stderr, "Unexpected rejection %d (%zu): %s\n", r.status, r.diagnostic_count,
                    source);
        assert(r.status == SEMANTIC_OK && r.complete && semantic_model_valid(m));
    } else {
        if (!r.diagnostic_count || r.diagnostics[0].code != (SemanticDiagnosticCode)expected)
            fprintf(stderr, "Unexpected diagnostic %d (%zu): %s\n",
                    r.diagnostic_count ? (int)r.diagnostics[0].code : -1, r.diagnostic_count,
                    source);
        assert(r.status == SEMANTIC_ERRORS && r.diagnostic_count &&
               r.diagnostics[0].code == (SemanticDiagnosticCode)expected);
    }
    semantic_model_destroy(m);
    ast_destroy(a);
}
static void contract_examples(void)
{
    check("main(){}", -1);
    check("main(){int x; x=1+2;}", -1);
    check("main(){float x; x=1+2.5;}", -1);
    check("main(){float x; x=1;}", SEM_DIAG_ASSIGNMENT);
    check("main(){float x; x=5.5%2;}", -1);
    check("main(){boolean b; b=1<2; if(b){}}", -1);
    check("main(){if(1){}}", SEM_DIAG_CONDITION);
    check("main(){int x; x=!1;}", SEM_DIAG_OPERATOR);
    check("main(){boolean a,b; a=b==b;}", SEM_DIAG_OPERATOR);
    check("int f(int x){int x; return x;} main(){}", SEM_DIAG_DUPLICATE);
    check("int f(int x){if(x>0){int x; x=2;} return x;} main(){}", -1);
    check("int f(){int x; x=func g(); return x;} int g(){return 1;} main(){}", -1);
    check("float f(float x){return x;} main(){func f(1)}", SEM_DIAG_ARGUMENT_TYPE);
    check("int f(){return 1;} main(){func f()}", -1);
    check("void f(){return 1;} main(){}", SEM_DIAG_RETURN_TYPE);
    check("int f(){if(1<2){return 1;}} main(){}", SEM_DIAG_MISSING_RETURN);
    check("int f(){if(1<2){return 1;}else{return 2;}} main(){}", -1);
    check("int f(){while(1<2){return 1;}} main(){}", SEM_DIAG_MISSING_RETURN);
    check("main(){return 0;}", -1);
    check("main(){println(\"ok\");}", -1);
    check("main(){char c; c=\"a\";}", SEM_DIAG_ASSIGNMENT);
    check("main(){int[2] a,b; a=b;}", -1);
    check("void f(int[3] row){} main(){int[2][3] m; func f(m[0])}", -1);
    check("main(){int[2][3] m; println(m[0]);}", SEM_DIAG_OUTPUT_TYPE);
    check("main(){int[2] a; println(a[2]);}", SEM_DIAG_INDEX_BOUNDS);
    check("main(){int x; x=-2147483648;}", -1);
    check("main(){int x; x=2147483648;}", SEM_DIAG_LITERAL_RANGE);
    check("main(){int x; x=1/0;}", SEM_DIAG_ZERO_DIVISOR);
    check("main(){int x; println(x);}", -1);
}
static void operators(void)
{
    const char *values[] = {"1", "1.5", "c", "b", "\"x\"", "a"};
    const char *ops[] = {"+", "-", "*", "/", "%", "&&", "||", "<", "<=", ">", ">=", "==", "!="};
    char source[256];
    for (size_t op = 0; op < 13; ++op)
        for (size_t a = 0; a < 6; ++a)
            for (size_t b = 0; b < 6; ++b) {
                bool numeric = a < 2 && b < 2;
                bool logical = op == 5 || op == 6;
                bool valid = logical ? (a == 3 && b == 3) : numeric;
                const char *target = (logical || op >= 7) ? "b" : a == 1 || b == 1 ? "f" : "i";
                snprintf(source, sizeof(source),
                         "main(){int i;float f;char c;boolean b;int[2] a; %s=%s%s%s;}", target,
                         values[a], ops[op], values[b]);
                check(source, valid ? -1 : SEM_DIAG_OPERATOR);
            }
    const char *unary[] = {"+", "-", "!"};
    for (size_t op = 0; op < 3; ++op)
        for (size_t a = 0; a < 6; ++a) {
            if (op != 2 && a == 4)
                continue; /* Signed text is rejected by the grammar. */
            bool valid = op == 2 ? a == 3 : a < 2;
            snprintf(source, sizeof(source),
                     "main(){int i;float f;char c;boolean b;int[2] a; %s=%s%s;}",
                     op == 2  ? "b"
                     : a == 1 ? "f"
                              : "i",
                     unary[op], values[a]);
            check(source, valid ? -1 : SEM_DIAG_OPERATOR);
        }
}
static void additional_rules(void)
{
    static const struct {
        const char *source;
        int expected;
    } cases[] = {
        {"main(){int x; x=x;}", -1},
        {"main(){int x; int x;}", SEM_DIAG_DUPLICATE},
        {"void f(){} void f(){} main(){}", SEM_DIAG_DUPLICATE},
        {"void f(int x,int x){} main(){}", SEM_DIAG_DUPLICATE},
        {"void f(){} main(){int f;func f()}", SEM_DIAG_NOT_FUNCTION},
        {"void f(){} main(){int x;x=f;}", SEM_DIAG_NOT_VARIABLE},
        {"main(){int x; x=missing;}", SEM_DIAG_UNDECLARED},
        {"main(){if(1<2){int x;} x=1;}", SEM_DIAG_UNDECLARED},
        {"main(){if(1<2){int x;} if(1<2){x=1;}}", SEM_DIAG_UNDECLARED},
        {"int f(int x){int y;y=func f(x);return y;} main(){func f(1)}", -1},
        {"int f(){int y;y=func g();return y;} int g(){int y;y=func f();return y;} main(){}", -1},
        {"int f(int x){return x;} main(){func f()}", SEM_DIAG_ARGUMENT_COUNT},
        {"int f(int x){return x;} main(){func f(1,2)}", SEM_DIAG_ARGUMENT_COUNT},
        {"void f(){} main(){int x;x=func f();}", SEM_DIAG_ASSIGNMENT},
        {"int f(){return 1.5;} main(){}", SEM_DIAG_RETURN_TYPE},
        {"float f(){return 1;} main(){}", SEM_DIAG_RETURN_TYPE},
        {"main(){return 1.5;}", SEM_DIAG_RETURN_TYPE},
        {"void f(){int x;x=1;} main(){}", -1},
        {"int f(){return 1;println(2);} main(){}", -1},
        {"int f(){for(;;){return 1;}} main(){}", SEM_DIAG_MISSING_RETURN},
        {"int f(){for(;;){}return 1;} main(){}", -1},
        {"main(){for(;;){}}", -1},
        {"main(){while(1){}}", SEM_DIAG_CONDITION},
        {"main(){for(;1;){}}", SEM_DIAG_CONDITION},
        {"main(){int[0] a;}", SEM_DIAG_DIMENSION},
        {"main(){int[999999999999999999999999999] a;}", SEM_DIAG_DIMENSION},
        {"main(){int[18446744073709551615][2] a;}", SEM_DIAG_DIMENSION},
        {"int[0] f(){return 1;} main(){}", SEM_DIAG_DIMENSION},
        {"void f(int[0] a){} main(){}", SEM_DIAG_DIMENSION},
        {"main(){int[2] a; int[3] b; a=b;}", SEM_DIAG_ASSIGNMENT},
        {"main(){int[2][3] a; int[3][2] b; a=b;}", SEM_DIAG_ASSIGNMENT},
        {"void f(int[2] x){} main(){int[3] a;func f(a)}", SEM_DIAG_ARGUMENT_TYPE},
        {"int[2] f(int[2] x){return x;} main(){int[2] a,b; b=func f(a);}", -1},
        {"int[3] f(int[2] x){return x;} main(){}", SEM_DIAG_RETURN_TYPE},
        {"main(){int x;println(x[0]);}", SEM_DIAG_INDEX_RANK},
        {"main(){int[2] a;println(a[0][0]);}", SEM_DIAG_INDEX_RANK},
        {"main(){int[2] a;println(a[1.5]);}", SEM_DIAG_INDEX_TYPE},
        {"main(){int[2] a;println(a[(1<2)]);}", SEM_DIAG_INDEX_TYPE},
        {"main(){int[2] a;println(a[-1]);}", SEM_DIAG_INDEX_BOUNDS},
        {"main(){int[2] a;println(a[1+1]);}", SEM_DIAG_INDEX_BOUNDS},
        {"main(){int i;int[2] a;i=2;println(a[i]);}", -1},
        {"main(){int[2][3] a;scanf(a);}", SEM_DIAG_INPUT_TYPE},
        {"main(){int[2][3] a;scanf(a[0]);}", SEM_DIAG_INPUT_TYPE},
        {"main(){char c;int i;float f;boolean b;int[2] "
         "a;scanf(c,i,f,b,a[0]);println(c,i,f,b,a[0],\"hi\");}",
         -1},
        {"main(){int x; x=\"a\"+\"b\";}", SEM_DIAG_OPERATOR},
        {"main(){int x; x=-2147483649;}", SEM_DIAG_LITERAL_RANGE},
        {"main(){int x; x=+2147483648;}", SEM_DIAG_LITERAL_RANGE},
        {"main(){int x; x=2147483647+1;}", -1},
        {"main(){int x; x=(-2147483648)*(-2147483648);}", -1},
        {"main(){int x; x=0-(-2147483648);}", -1},
        {"main(){int x; x=(-2147483648)/(-1);}", SEM_DIAG_DIVISION_OVERFLOW},
        {"main(){int x; x=(-2147483648)%(-1);}", SEM_DIAG_DIVISION_OVERFLOW},
        {"main(){float x; x=1.5/0;}", SEM_DIAG_ZERO_DIVISOR},
        {"main(){float x; x=1.5%0.0;}", SEM_DIAG_ZERO_DIVISOR},
        {"main(){int x; x=x/(1-1);}", SEM_DIAG_ZERO_DIVISOR},
        {"main(){boolean b;b=(1>2)&&(missing>1);}", SEM_DIAG_UNDECLARED},
        {"main(){boolean b;b=(1<2)||(1/0>1);}", SEM_DIAG_ZERO_DIVISOR}};
    for (size_t j = 0; j < sizeof(cases) / sizeof(*cases); ++j)
        check(cases[j].source, cases[j].expected);
#if SIZE_MAX == UINT64_MAX
    check("main(){boolean[18446744073709551615] b;println(b[-2]);}", SEM_DIAG_INDEX_BOUNDS);
#endif
    char huge[600];
    strcpy(huge, "main(){float x;x=");
    size_t start = strlen(huge);
    memset(huge + start, '9', 400);
    strcpy(huge + start + 400, ".0;}");
    check(huge, SEM_DIAG_LITERAL_RANGE);
}
static NodeId first_kind(const Ast *a, AstKind kind)
{
    for (size_t j = 0; j < ast_count(a); ++j)
        if (ast_node(a, j)->kind == kind)
            return j;
    assert(false);
    return AST_NODE_NONE;
}
static void model_data(void)
{
    Ast *a = parse("int[2][3] copy(int[2][3] arg){return arg;} main(){int[2][3] x,y;float f;y=func "
                   "copy(x);f=1+2.5;println(x[1][2],\"hi\");}");
    size_t count = ast_count(a);
    AstNode *snapshot = malloc(count * sizeof(*snapshot));
    assert(snapshot);
    for (size_t j = 0; j < count; ++j)
        snapshot[j] = *ast_node(a, j);
    SemanticModel *m = NULL;
    assert(semantic_analyze(a, NULL, &m).status == SEMANTIC_OK);
    assert(semantic_model_valid(m));
    for (size_t j = 0; j < count; ++j) {
        assert(!memcmp(&snapshot[j], ast_node(a, j), sizeof(*snapshot)));
        const SemanticInfo *i = semantic_model_info(m, j);
        assert(i && i->state == SEM_VALID);
        assert(i->scope != SCOPE_ID_NONE);
        if (i->symbol != SYMBOL_ID_NONE) {
            const SemanticSymbol *s = semantic_symbol(semantic_model_symbols(m), i->symbol);
            assert(s);
            if (ast_node(a, j)->kind != AST_INDEX)
                assert(i->type.concrete.dimensions == s->type.dimensions);
        }
    }
    free(snapshot);
    NodeId binary = first_kind(a, AST_BINARY);
    const AstNode *bn = ast_node(a, binary);
    const SemanticInfo *bi = semantic_model_info(m, binary);
    assert(bi->type.concrete.base == TYPE_FLOAT && bi->left_conversion == SEM_CONVERT_INT_FLOAT &&
           bi->right_conversion == SEM_CONVERT_NONE);
    assert(semantic_model_info(m, bn->as.binary.left)->type.concrete.base == TYPE_INT);
    assert(bi->constant.kind == SEM_CONSTANT_FLOAT && bi->constant.as.decimal == 3.5);
    NodeId text = first_kind(a, AST_TEXT_LITERAL);
    const SemanticInfo *ti = semantic_model_info(m, text);
    assert(ti->type.is_string && ti->constant.kind == SEM_CONSTANT_TEXT);
    assert(ti->constant.as.text.length == 2 && !memcmp(ti->constant.as.text.data, "hi", 2));
    assert(semantic_model_info(m, ast_root(a))->type_state == SEM_NOT_APPLICABLE);
    assert(!semantic_model_info(m, AST_NODE_NONE));
    assert(semantic_current_scope(semantic_model_symbols(m)) == SCOPE_GLOBAL);
    semantic_model_destroy(m);
    ast_destroy(a);
    static const struct {
        const char *expr;
        int64_t integer;
    } integers[] = {{"-7/3", -2}, {"-7%3", -1},  {"7%-3", 1},   {"(-2147483648)", INT32_MIN},
                    {"+2", 2},    {"2+3*4", 14}, {"20-3-2", 15}};
    for (size_t k = 0; k < sizeof(integers) / sizeof(*integers); ++k) {
        char source[120];
        snprintf(source, sizeof(source), "main(){int x;x=%s;}", integers[k].expr);
        a = parse(source);
        assert(semantic_analyze(a, NULL, &m).status == SEMANTIC_OK);
        const AstNode *assign = ast_node(a, first_kind(a, AST_ASSIGN));
        const SemanticInfo *v = semantic_model_info(m, assign->as.assign.value);
        assert(v->constant.kind == SEM_CONSTANT_INT &&
               v->constant.as.integer == integers[k].integer);
        semantic_model_destroy(m);
        ast_destroy(a);
    }
    static const struct {
        const char *expression;
        bool expected;
    } booleans[] = {{"1<2", true},     {"2<=2", true},         {"3>5", false},
                    {"2>=2", true},    {"1==1.0", true},       {"1!=2", true},
                    {"!(1<2)", false}, {"(1<2)&&(2<3)", true}, {"(1>2)||(2<3)", true}};
    for (size_t j = 0; j < sizeof(booleans) / sizeof(*booleans); ++j) {
        char source[120];
        snprintf(source, sizeof(source), "main(){boolean b;b=%s;}", booleans[j].expression);
        a = parse(source);
        assert(semantic_analyze(a, NULL, &m).status == SEMANTIC_OK);
        const AstNode *assign = ast_node(a, first_kind(a, AST_ASSIGN));
        const SemanticInfo *v = semantic_model_info(m, assign->as.assign.value);
        assert(v->constant.kind == SEM_CONSTANT_BOOLEAN &&
               v->constant.as.boolean == booleans[j].expected);
        semantic_model_destroy(m);
        ast_destroy(a);
    }
    a = parse("main(){float f;f=-5.5%2;}");
    assert(semantic_analyze(a, NULL, &m).status == SEMANTIC_OK);
    const AstNode *fa = ast_node(a, first_kind(a, AST_ASSIGN));
    assert(semantic_model_info(m, fa->as.assign.value)->constant.as.decimal == -1.5);
    semantic_model_destroy(m);
    ast_destroy(a);
    check("main(){int x,y;x=0;y=1/x;}", -1);
    check("main(){if(1>2){println(missing);}}", SEM_DIAG_UNDECLARED);
    a = parse("main(){int x;int[2] a;x=2;println(a[x]);}");
    assert(semantic_analyze(a, NULL, &m).status == SEMANTIC_OK);
    assert(semantic_model_info(m, first_kind(a, AST_INDEX))->constant.kind == SEM_CONSTANT_NONE);
    semantic_model_destroy(m);
    ast_destroy(a);
    const char *locale = setlocale(LC_NUMERIC, NULL);
    char saved[256];
    snprintf(saved, sizeof(saved), "%s", locale);
    const char *candidates[] = {"pt_BR.UTF-8", "de_DE.UTF-8", "pt_BR.utf8"};
    for (size_t k = 0; k < 3; ++k)
        if (setlocale(LC_NUMERIC, candidates[k]))
            break;
    check("main(){float x;x=1.5;}", -1);
    assert(setlocale(LC_NUMERIC, saved));
}
static void diagnostics(void)
{
    const char *source = "main(){int x;\n x=missing+1;\n x=other+2;\n x=3;}";
    Ast *a = parse(source);
    SemanticModel *m = NULL;
    SemanticResult r = semantic_analyze(a, NULL, &m);
    assert(r.status == SEMANTIC_ERRORS && r.complete && r.diagnostic_count == 2 &&
           !r.limit_reached);
    assert(r.diagnostics[0].code == SEM_DIAG_UNDECLARED && r.diagnostics[0].span.line == 2 &&
           r.diagnostics[0].span.column == 4 && r.diagnostics[0].span.source == 7);
    assert(r.diagnostics[1].span.line == 3);
    size_t valid_assign = 0;
    for (size_t j = 0; j < ast_count(a); ++j)
        if (ast_node(a, j)->kind == AST_ASSIGN && semantic_model_info(m, j)->state == SEM_VALID)
            ++valid_assign;
    assert(valid_assign == 1 && !semantic_model_valid(m));
    semantic_model_destroy(m);
    ast_destroy(a);
    a = parse("main(){int[0] x; x=x;}");
    r = semantic_analyze(a, NULL, &m);
    assert(r.diagnostic_count == 1 && r.diagnostics[0].code == SEM_DIAG_DIMENSION);
    semantic_model_destroy(m);
    ast_destroy(a);
    a = parse("void bad(int[0] a){} main(){func bad(x)}");
    r = semantic_analyze(a, NULL, &m);
    assert(r.diagnostic_count == 2 && r.diagnostics[1].code == SEM_DIAG_UNDECLARED);
    semantic_model_destroy(m);
    ast_destroy(a);
    char source_many[1024] = "main(){int x;";
    for (size_t j = 0; j < 40; ++j)
        strcat(source_many, "x=bad;");
    strcat(source_many, "}");
    a = parse(source_many);
    r = semantic_analyze(a, NULL, &m);
    assert(r.diagnostic_count == 32 && r.limit_reached && !r.complete && !semantic_model_valid(m));
    size_t unanalyzed = 0;
    for (size_t j = 0; j < ast_count(a); ++j)
        unanalyzed += semantic_model_info(m, j)->state == SEM_UNANALYZED;
    assert(unanalyzed);
    semantic_model_destroy(m);
    ast_destroy(a);
    assert(semantic_analyze(NULL, NULL, &m).status == SEMANTIC_INVALID_INPUT && !m);
    assert(semantic_analyze(NULL, NULL, NULL).status == SEMANTIC_INVALID_INPUT);
    assert(!semantic_model_valid(NULL) && !semantic_model_info(NULL, 0) &&
           !semantic_model_result(NULL) && !semantic_model_symbols(NULL));
    Ast *unpublished = NULL;
    assert(ast_create(&unpublished, NULL) == PARSE_OK);
    assert(semantic_analyze(unpublished, NULL, &m).status == SEMANTIC_INVALID_INPUT && !m);
    ast_destroy(unpublished);
    semantic_model_destroy(NULL);
    for (unsigned j = 0; j <= SEM_DIAG_INVALID_AST; ++j)
        assert(strlen(semantic_diagnostic_message((SemanticDiagnosticCode)j)) > 4);
}
int main(void)
{
    contract_examples();
    operators();
    additional_rules();
    model_data();
    diagnostics();
    printf("semantic_analysis_test: %zu contract/operator cases + model and diagnostic checks OK\n",
           checks);
}
