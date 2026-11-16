#include "../parser/ast.h"
#include "../lexer/lexer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool covered[AST_KIND_COUNT];
static Ast *parse(const char *source)
{
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));
    ParserStream *p = NULL;
    assert(parser_stream_create_ast(&p, NULL, 17) == PARSE_OK);
    for (;;) {
        Token t = lexer_next(&lexer);
        ParserStep s = parser_stream_feed(p, t);
        assert(s.consumed);
        if (t.kind == TOK_EOF || s.flow != PARSER_NEED_TOKEN)
            break;
    }
    Ast *a = parser_stream_take_ast(p);
    if (!a) {
        const ParseResult *r = parser_stream_result(p);
        fprintf(stderr, "parse AST status %d in %s\n", r->status, source);
    }
    assert(a && parser_stream_result(p)->status == PARSE_OK);
    assert(!parser_stream_take_ast(p));
    parser_stream_destroy(p);
    for (size_t i = 0; i < ast_count(a); ++i) {
        const AstNode *n = ast_node(a, i);
        assert(n && n->id == i && n->span.source == 17);
        assert(n->span.first_token < n->span.last_token);
        assert(n->span.line && n->span.column && n->span.end_line && n->span.end_column);
        covered[n->kind] = true;
    }
    assert(ast_publish(a, ast_root(a)) == PARSE_OK);
    assert(ast_append(a, &(AstNode){.kind = AST_INT_LITERAL}) == AST_NODE_NONE);
    return a;
}
static const AstNode *node(const Ast *a, NodeId id, AstKind kind)
{
    const AstNode *n = ast_node(a, id);
    assert(n && n->kind == kind);
    return n;
}
static const AstNode *body(const Ast *a)
{
    const AstNode *p = node(a, ast_root(a), AST_PROGRAM);
    return node(a, node(a, p->as.program.main, AST_MAIN)->as.main.body, AST_BLOCK);
}
static void text(AstText value, const char *expected)
{
    assert(value.length == strlen(expected) && !memcmp(value.data, expected, value.length));
}
static const AstNode *statement(const Ast *a, size_t i, AstKind kind)
{
    return node(a, body(a)->as.block.statements.items[i], kind);
}
static const AstNode *value(const Ast *a)
{
    return ast_node(a, statement(a, 0, AST_ASSIGN)->as.assign.value);
}
static void expressions(void)
{
    Ast *a = parse("main(){int x; x=10-3-2*4;}");
    const AstNode *n = value(a);
    assert(n->kind == AST_BINARY && n->as.binary.op == AST_SUBTRACT);
    const AstNode *left = node(a, n->as.binary.left, AST_BINARY);
    assert(left->as.binary.op == AST_SUBTRACT);
    text(node(a, left->as.binary.left, AST_INT_LITERAL)->as.literal.lexeme, "10");
    const AstNode *right = node(a, n->as.binary.right, AST_BINARY);
    assert(right->as.binary.op == AST_MULTIPLY);
    ast_destroy(a);
    a = parse("main(){x=(1+2)*3;}");
    n = value(a);
    assert(n->as.binary.op == AST_MULTIPLY);
    left = node(a, n->as.binary.left, AST_BINARY);
    assert(left->as.binary.op == AST_ADD);
    assert(left->span.column == 10 && left->span.end_column == 15);
    assert(left->as.binary.operator_span.column == 12);
    ast_destroy(a);
    a = parse("main(){x=!!-2;}");
    n = value(a);
    assert(n->kind == AST_UNARY && n->as.unary.op == AST_NOT);
    n = node(a, n->as.unary.operand, AST_UNARY);
    assert(n->as.unary.op == AST_NOT);
    n = node(a, n->as.unary.operand, AST_UNARY);
    assert(n->as.unary.op == AST_NEGATIVE);
    text(node(a, n->as.unary.operand, AST_INT_LITERAL)->as.literal.lexeme, "2");
    ast_destroy(a);
    a = parse("main(){x=+a||b&&c<d+e*f;}");
    n = value(a);
    assert(n->as.binary.op == AST_OR);
    left = node(a, n->as.binary.left, AST_UNARY);
    assert(left->as.unary.op == AST_POSITIVE);
    n = node(a, n->as.binary.right, AST_BINARY);
    assert(n->as.binary.op == AST_AND);
    n = node(a, n->as.binary.right, AST_BINARY);
    assert(n->as.binary.op == AST_LT);
    n = node(a, n->as.binary.right, AST_BINARY);
    assert(n->as.binary.op == AST_ADD);
    n = node(a, n->as.binary.right, AST_BINARY);
    assert(n->as.binary.op == AST_MULTIPLY);
    ast_destroy(a);
    static const char *ops[] = {"<", "<=", ">", ">=", "==", "!="};
    static const AstBinaryOperator expected[] = {AST_LT, AST_LE, AST_GT, AST_GE, AST_EQ, AST_NE};
    for (size_t i = 0; i < 6; ++i) {
        char source[64];
        snprintf(source, sizeof(source), "main(){x=a%sb;}", ops[i]);
        a = parse(source);
        assert(value(a)->as.binary.op == expected[i]);
        ast_destroy(a);
    }
    a = parse("main(){x=12/3%2;}");
    n = value(a);
    assert(n->as.binary.op == AST_REMAINDER);
    assert(node(a, n->as.binary.left, AST_BINARY)->as.binary.op == AST_DIVIDE);
    ast_destroy(a);
    a = parse("main(){x=a&&b&&c||d||e;}");
    n = value(a);
    assert(n->as.binary.op == AST_OR);
    n = node(a, n->as.binary.left, AST_BINARY);
    assert(n->as.binary.op == AST_OR);
    n = node(a, n->as.binary.left, AST_BINARY);
    assert(n->as.binary.op == AST_AND);
    assert(node(a, n->as.binary.left, AST_BINARY)->as.binary.op == AST_AND);
    ast_destroy(a);
}
static void declarations_calls_io(void)
{
    Ast *a = parse("float f(int[2][3] m,float y){int a,b; return y;} void g(){} main(){char c; "
                   "boolean flag; int[0][999999999999999999999999] m,n; x=func f(m,2.5) ; func g() "
                   "scanf(m[1][2],c); println(m[1],\"oi\",2,3.5);}");
    const AstNode *p = node(a, ast_root(a), AST_PROGRAM);
    assert(p->as.program.functions.count == 2);
    const AstNode *f = node(a, p->as.program.functions.items[0], AST_FUNCTION_DECL);
    text(f->as.function.name.text, "f");
    assert(f->as.function.return_type.base == AST_FLOAT && f->as.function.parameters.count == 2);
    const AstNode *param = node(a, f->as.function.parameters.items[0], AST_PARAM_DECL);
    text(param->as.declaration.name.text, "m");
    assert(param->as.declaration.type.rank == 2);
    text(param->as.declaration.type.dimensions[0].lexeme, "2");
    text(param->as.declaration.type.dimensions[1].lexeme, "3");
    const AstNode *second = node(a, f->as.function.parameters.items[1], AST_PARAM_DECL);
    text(second->as.declaration.name.text, "y");
    assert(second->as.declaration.type.base == AST_FLOAT);
    const AstNode *b = node(a, f->as.function.body, AST_BLOCK);
    assert(b->as.block.declarations.count == 2);
    text(node(a, b->as.block.declarations.items[0], AST_VAR_DECL)->as.declaration.name.text, "a");
    text(node(a, b->as.block.declarations.items[1], AST_VAR_DECL)->as.declaration.name.text, "b");
    node(a, b->as.block.statements.items[0], AST_RETURN_STMT);
    f = node(a, p->as.program.functions.items[1], AST_FUNCTION_DECL);
    text(f->as.function.name.text, "g");
    assert(f->as.function.return_type.base == AST_VOID && !f->as.function.parameters.count);
    b = body(a);
    assert(b->as.block.declarations.count == 4);
    const AstNode *decl = node(a, b->as.block.declarations.items[2], AST_VAR_DECL);
    text(decl->as.declaration.name.text, "m");
    assert(decl->as.declaration.type.rank == 2);
    text(decl->as.declaration.type.dimensions[0].lexeme, "0");
    text(decl->as.declaration.type.dimensions[1].lexeme, "999999999999999999999999");
    text(node(a, b->as.block.declarations.items[3], AST_VAR_DECL)->as.declaration.name.text, "n");
    const AstNode *call = node(a, statement(a, 0, AST_ASSIGN)->as.assign.value, AST_CALL);
    text(call->as.call.callee.text, "f");
    assert(call->as.call.arguments.count == 2);
    text(node(a, call->as.call.arguments.items[1], AST_FLOAT_LITERAL)->as.literal.lexeme, "2.5");
    call = node(a, statement(a, 1, AST_CALL_STMT)->as.call_stmt.call, AST_CALL);
    assert(!call->as.call.arguments.count);
    const AstNode *io = statement(a, 2, AST_READ_STMT);
    assert(io->as.io.items.count == 2);
    const AstNode *index = node(a, io->as.io.items.items[0], AST_INDEX);
    text(node(a, index->as.index.base, AST_NAME)->as.name.text, "m");
    assert(index->as.index.indices.count == 2);
    text(node(a, index->as.index.indices.items[0], AST_INT_LITERAL)->as.literal.lexeme, "1");
    text(node(a, index->as.index.indices.items[1], AST_INT_LITERAL)->as.literal.lexeme, "2");
    io = statement(a, 3, AST_WRITE_STMT);
    assert(io->as.io.items.count == 4);
    text(node(a, io->as.io.items.items[1], AST_TEXT_LITERAL)->as.literal.lexeme, "\"oi\"");
    ast_destroy(a);
    a = parse("int[2][3] f(){int[2][3] a; return a;} main(){}");
    p = node(a, ast_root(a), AST_PROGRAM);
    f = node(a, p->as.program.functions.items[0], AST_FUNCTION_DECL);
    assert(f->as.function.return_type.rank == 2);
    ast_destroy(a);
}
static void controls(void)
{
    Ast *a = parse("main(){if(x){while(y){return 1;}}else{} if(x){} for(;;){} "
                   "for(i=0;i<2;i=i+1){println(i);}}");
    const AstNode *n = statement(a, 0, AST_IF_STMT);
    assert(n->as.if_stmt.else_block != AST_NODE_NONE);
    const AstNode *b = node(a, n->as.if_stmt.then_block, AST_BLOCK);
    node(a, b->as.block.statements.items[0], AST_WHILE_STMT);
    assert(statement(a, 1, AST_IF_STMT)->as.if_stmt.else_block == AST_NODE_NONE);
    n = statement(a, 2, AST_FOR_STMT);
    assert(n->as.for_stmt.initializer == AST_NODE_NONE &&
           n->as.for_stmt.condition == AST_NODE_NONE && n->as.for_stmt.update == AST_NODE_NONE);
    n = statement(a, 3, AST_FOR_STMT);
    node(a, n->as.for_stmt.initializer, AST_ASSIGN);
    node(a, n->as.for_stmt.condition, AST_BINARY);
    node(a, n->as.for_stmt.update, AST_ASSIGN);
    ast_destroy(a);
    for (unsigned mask = 0; mask < 8; ++mask) {
        char source[128];
        snprintf(source, sizeof(source), "main(){for(%s;%s;%s){}}", mask & 1 ? "i=0" : "",
                 mask & 2 ? "i<2" : "", mask & 4 ? "i=i+1" : "");
        a = parse(source);
        n = statement(a, 0, AST_FOR_STMT);
        assert((n->as.for_stmt.initializer != AST_NODE_NONE) == !!(mask & 1));
        assert((n->as.for_stmt.condition != AST_NODE_NONE) == !!(mask & 2));
        assert((n->as.for_stmt.update != AST_NODE_NONE) == !!(mask & 4));
        ast_destroy(a);
    }
}
static void positions_and_lifetime(void)
{
    char source[] = "// before\nmain(){\n x = (12); // after\n}";
    Ast *a = parse(source);
    memset(source, '?', sizeof(source) - 1);
    const AstNode *n = statement(a, 0, AST_ASSIGN);
    assert(n->span.line == 3 && n->span.column == 2 && n->span.end_column == 11);
    n = node(a, n->as.assign.target, AST_NAME);
    text(n->as.name.text, "x");
    assert(n->as.name.span.line == 3 && n->as.name.span.column == 2);
    n = value(a);
    text(n->as.literal.lexeme, "12");
    assert(n->span.column == 6 && n->span.end_column == 10 &&
           n->as.literal.literal_span.column == 7 && n->as.literal.literal_span.end_column == 9);
    const AstNode *p = node(a, ast_root(a), AST_PROGRAM);
    assert(p->span.first_token == 1 && p->span.last_token == 13 && p->span.end_line == 4 &&
           p->span.end_column == 2);
    ast_destroy(a);
    /* Reusing the same token text storage after each feed is supported. */
    Lexer lexer;
    const char *input = "main(){x=123+456;}";
    lexer_init(&lexer, input, strlen(input));
    ParserStream *stream = NULL;
    assert(parser_stream_create_ast(&stream, NULL, 0) == PARSE_OK);
    char scratch[16];
    for (;;) {
        Token t = lexer_next(&lexer);
        assert(t.length < sizeof(scratch));
        memcpy(scratch, t.begin, t.length);
        t.begin = scratch;
        ParserStep s = parser_stream_feed(stream, t);
        memset(scratch, '?', sizeof(scratch));
        assert(s.consumed);
        if (t.kind == TOK_EOF)
            break;
    }
    a = parser_stream_take_ast(stream);
    assert(a);
    parser_stream_destroy(stream);
    n = value(a);
    text(node(a, n->as.binary.left, AST_INT_LITERAL)->as.literal.lexeme, "123");
    text(node(a, n->as.binary.right, AST_INT_LITERAL)->as.literal.lexeme, "456");
    ast_destroy(a);
}
static void invalid_inputs(void)
{
    static const char *cases[] = {"",
                                  "main(){x=; y=2;}",
                                  "main(){int x x=1;}",
                                  "main(){if(x){x=;}}",
                                  "main(){x=1;",
                                  "main(){@}",
                                  "main(){return;}",
                                  "main(){println(x+1);}",
                                  "main(){m[0]=1;}",
                                  "main(){return func f();}",
                                  "main(){x=func f()+1;}",
                                  "main(){for(i=func f();;){}}"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        Token tokens[256];
        size_t count = 0;
        Lexer lexer;
        lexer_init(&lexer, cases[i], strlen(cases[i]));
        do {
            assert(count < 256);
            tokens[count++] = lexer_next(&lexer);
        } while (tokens[count - 1].kind != TOK_EOF);
        ParseResult expected = parser_parse(tokens, count);
        assert(expected.status != PARSE_OK);
        Ast *a = (Ast *)1;
        ParseResult actual = parser_parse_ast(tokens, count, 0, &a);
        assert(!a);
        assert(expected.status == actual.status &&
               expected.diagnostic_count == actual.diagnostic_count);
        for (size_t j = 0; j < expected.diagnostic_count; ++j) {
            assert(expected.diagnostics[j].token_index == actual.diagnostics[j].token_index);
            assert(!memcmp(expected.diagnostics[j].expected, actual.diagnostics[j].expected,
                           sizeof(actual.diagnostics[j].expected)));
        }
    }
    Ast *a = (Ast *)1;
    assert(parser_parse_ast(NULL, 0, 0, &a).status == PARSE_INVALID_INPUT && !a);
    Token tokens[] = {{.kind = TOK_EOF}, {.kind = TOK_MAIN}};
    assert(parser_parse_ast(tokens, 2, 0, &a).status == PARSE_INVALID_INPUT && !a);
    ParserStream *p = NULL;
    assert(parser_stream_create_ast(&p, NULL, 0) == PARSE_OK);
    assert(!parser_stream_take_ast(p));
    assert(parser_stream_finish(p).status == PARSE_INVALID_INPUT);
    assert(!parser_stream_take_ast(p));
    parser_stream_destroy(p);
    /* Recognizer accepts artificial TOK_COMP; AST requires an actual operator. */
    const char *input = "main(){x=a<b;}";
    Lexer lexer;
    lexer_init(&lexer, input, strlen(input));
    assert(parser_stream_create_ast(&p, NULL, 0) == PARSE_OK);
    for (;;) {
        Token t = lexer_next(&lexer);
        if (t.kind == TOK_COMP) {
            t.begin = "?";
            t.length = 1;
        }
        ParserStep step = parser_stream_feed(p, t);
        if (step.flow != PARSER_NEED_TOKEN)
            break;
    }
    assert(parser_stream_result(p)->status == PARSE_INVALID_INPUT && !parser_stream_take_ast(p));
    parser_stream_destroy(p);
}
static void manual(void)
{
    Ast *a = NULL;
    assert(ast_create(&a, NULL) == PARSE_OK);
    NodeId block = ast_append(a, &(AstNode){.kind = AST_BLOCK});
    assert(block == 0);
    NodeId main = ast_append(a, &(AstNode){.kind = AST_MAIN, .as.main = {block}});
    NodeId root = ast_append(a, &(AstNode){.kind = AST_PROGRAM, .as.program = {{0}, main}});
    assert(ast_publish(a, root) == PARSE_OK && ast_root(a) == root);
    ast_destroy(a);
    assert(ast_create(&a, NULL) == PARSE_OK);
    block = ast_append(a, &(AstNode){.kind = AST_BLOCK});
    root = ast_append(a, &(AstNode){.kind = AST_PROGRAM, .as.program = {{0}, block}});
    assert(ast_publish(a, root) == PARSE_INVALID_INPUT);
    ast_destroy(a);
    assert(ast_create(&a, NULL) == PARSE_OK);
    ast_append(a, &(AstNode){.kind = AST_NAME, .as.name = {{"orphan", 6}, {0}}});
    block = ast_append(a, &(AstNode){.kind = AST_BLOCK});
    main = ast_append(a, &(AstNode){.kind = AST_MAIN, .as.main = {block}});
    root = ast_append(a, &(AstNode){.kind = AST_PROGRAM, .as.program = {{0}, main}});
    assert(ast_publish(a, root) == PARSE_INVALID_INPUT);
    ast_destroy(a);
    assert(ast_create(&a, NULL) == PARSE_OK);
    assert(ast_append(a, &(AstNode){.kind = AST_NAME, .as.name = {{NULL, 2}, {0}}}) ==
           AST_NODE_NONE);
    assert(ast_status(a) == PARSE_INVALID_INPUT);
    ast_destroy(a);
    ast_destroy(NULL);
    assert(!ast_node(NULL, 0) && ast_root(NULL) == AST_NODE_NONE && ast_count(NULL) == 0);
}
int main(void)
{
    expressions();
    declarations_calls_io();
    controls();
    positions_and_lifetime();
    invalid_inputs();
    manual();
    for (size_t i = 0; i < AST_KIND_COUNT; ++i)
        assert(covered[i]);
    puts("ast_test: OK (22 kinds, shape, operators, order, spans, lifetime, invalid input, manual "
         "construction)");
}
