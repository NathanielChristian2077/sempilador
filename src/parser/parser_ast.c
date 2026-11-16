#include "parser_ast_internal.h"
#include <string.h>

/* Synthesized attributes. Lists are persistent head/tail chains: reducing a
 * right-recursive LL tail is O(1); flattening and left folds happen once. */
typedef struct Link {
    struct Link *next;
    NodeId node;
    AstName name;
    AstDimension dimension;
    AstBinaryOperator op;
    SourceSpan operator_span;
} Link;
typedef struct {
    SourceSpan span;
    Token token;
    NodeId node;
    TypeSyntax type;
    Link *list;
    size_t count;
    AstBinaryOperator op;
} Value;
typedef struct {
    LL1ProductionId production;
    size_t base;
} Frame;
struct AstBuilder {
    Ast *ast;
    ParserAllocator allocator;
    uint32_t source;
    Frame *frames;
    size_t depth, frame_capacity;
    Value *values;
    size_t count, value_capacity;
    NodeId root;
};
static SourceSpan join(SourceSpan a, SourceSpan b)
{
    a.last_token = b.last_token;
    a.end_line = b.end_line;
    a.end_column = b.end_column;
    return a;
}
static AstName name(Value v)
{
    return (AstName){{v.token.begin, v.token.length}, v.span};
}
static NodeId emit(AstBuilder *b, AstNode n)
{
    return ast_append(b->ast, &n);
}
static Value empty(void)
{
    return (Value){.node = AST_NODE_NONE};
}
static Value prepend(AstBuilder *b, Link item, Value tail)
{
    Link *link = ast_alloc(b->ast, sizeof(*link));
    if (!link)
        return empty();
    *link = item;
    link->next = tail.list;
    tail.list = link;
    ++tail.count;
    return tail;
}
static AstList list(AstBuilder *b, Value v)
{
    if (!v.count)
        return (AstList){0};
    if (v.count > SIZE_MAX / sizeof(NodeId)) {
        ast_fail(b->ast, PARSE_LIMIT_EXCEEDED);
        return (AstList){0};
    }
    NodeId *items = ast_alloc(b->ast, v.count * sizeof(*items));
    if (!items)
        return (AstList){0};
    Link *p = v.list;
    for (size_t i = 0; i < v.count; ++i, p = p->next)
        items[i] = p->node;
    return (AstList){items, v.count};
}
static NodeId literal(AstBuilder *b, Value v)
{
    AstKind kind = v.token.kind == TOK_NUM_INT   ? AST_INT_LITERAL
                   : v.token.kind == TOK_NUM_DEC ? AST_FLOAT_LITERAL
                                                 : AST_TEXT_LITERAL;
    return emit(b, (AstNode){.kind = kind,
                             .span = v.span,
                             .as.literal = {{v.token.begin, v.token.length}, v.span}});
}
static NodeId identifier(AstBuilder *b, Value id, Value indices)
{
    NodeId base = emit(b, (AstNode){.kind = AST_NAME, .span = id.span, .as.name = name(id)});
    if (!indices.count)
        return base;
    return emit(b, (AstNode){.kind = AST_INDEX,
                             .span = join(id.span, indices.span),
                             .as.index = {base, list(b, indices)}});
}
static NodeId declaration(AstBuilder *b, AstKind kind, TypeSyntax type, Value id)
{
    return emit(b, (AstNode){.kind = kind,
                             .span = join(type.span, id.span),
                             .as.declaration = {name(id), type}});
}
static NodeId fold(AstBuilder *b, NodeId left, Value tail)
{
    for (Link *p = tail.list; p && ast_status(b->ast) == PARSE_OK; p = p->next) {
        const AstNode *l = ast_node(b->ast, left), *r = ast_node(b->ast, p->node);
        if (!l || !r) {
            ast_fail(b->ast, PARSE_INVALID_INPUT);
            return AST_NODE_NONE;
        }
        SourceSpan span = join(l->span, r->span);
        left = emit(b, (AstNode){.kind = AST_BINARY,
                                 .span = span,
                                 .as.binary = {p->op, left, p->node, p->operator_span}});
    }
    return left;
}
static AstBinaryOperator comparison(AstBuilder *b, Token t)
{
    static const struct {
        const char *text;
        size_t length;
        AstBinaryOperator op;
    } ops[] = {{"<", 1, AST_LT},  {"<=", 2, AST_LE}, {">", 1, AST_GT},
               {">=", 2, AST_GE}, {"==", 2, AST_EQ}, {"!=", 2, AST_NE}};
    for (size_t i = 0; i < sizeof(ops) / sizeof(*ops); ++i)
        if (t.length == ops[i].length && !memcmp(t.begin, ops[i].text, t.length))
            return ops[i].op;
    ast_fail(b->ast, PARSE_INVALID_INPUT);
    return AST_EQ;
}
static Value reduce(AstBuilder *b, LL1Production rule, Value *v)
{
    Value out = empty();
    if (!rule.rhs_length)
        return out;
    SourceSpan span = join(v[0].span, v[rule.rhs_length - 1].span);
    out.span = span;
    switch ((LL1Nonterminal)rule.lhs) {
    case LL1_NT_TIPOBASE:
        out.type.base = v[0].token.kind == TOK_CHAR    ? AST_CHAR
                        : v[0].token.kind == TOK_FLOAT ? AST_FLOAT
                        : v[0].token.kind == TOK_INT   ? AST_INT
                                                       : AST_BOOLEAN;
        out.type.span = span;
        break;
    case LL1_NT_TIPORETORNO:
        if (v[0].token.kind == TOK_VOID)
            out.type = (TypeSyntax){.base = AST_VOID, .span = span};
        else
            out = v[0];
        break;
    case LL1_NT_TIPO: {
        out.type = v[0].type;
        out.type.rank = v[1].count;
        out.type.span = v[1].count ? span : v[0].span;
        if (v[1].count) {
            if (v[1].count > SIZE_MAX / sizeof(AstDimension)) {
                ast_fail(b->ast, PARSE_LIMIT_EXCEEDED);
                break;
            }
            AstDimension *dims = ast_alloc(b->ast, v[1].count * sizeof(*dims));
            if (!dims)
                break;
            Link *p = v[1].list;
            for (size_t i = 0; i < v[1].count; ++i, p = p->next)
                dims[i] = p->dimension;
            out.type.dimensions = dims;
        }
        out.span = out.type.span;
        break;
    }
    case LL1_NT_DIMENSAO:
        out = prepend(b, (Link){.dimension = {{v[1].token.begin, v[1].token.length}, v[1].span}},
                      v[3]);
        out.span = v[3].count ? span : join(v[0].span, v[2].span);
        break;
    case LL1_NT_LISTAID:
        out = prepend(b, (Link){.name = name(v[1])}, v[2]);
        out.span = span;
        break;
    case LL1_NT_CONSTANTE:
        out.node = literal(b, v[0]);
        break;
    case LL1_NT_SINAL:
        out.token = v[0].token;
        break;
    case LL1_NT_OP_ADITIVO:
        out.op = v[0].token.kind == TOK_PLUS ? AST_ADD : AST_SUBTRACT;
        break;
    case LL1_NT_OP_MULTIPLICATIVO:
        out.op = v[0].token.kind == TOK_STAR    ? AST_MULTIPLY
                 : v[0].token.kind == TOK_SLASH ? AST_DIVIDE
                                                : AST_REMAINDER;
        break;
    case LL1_NT_DIMENSAO2:
        out = prepend(b, (Link){.node = v[1].node}, v[3]);
        out.span = v[3].count ? span : join(v[0].span, v[2].span);
        break;
    case LL1_NT_TERMO:
    case LL1_NT_TERMOLEITURA:
    case LL1_NT_TERMOESCRITA:
        if (v[0].token.kind == TOK_ID)
            out.node = identifier(b, v[0], v[1]);
        else if (v[0].token.kind == TOK_TEXTO)
            out.node = literal(b, v[0]);
        else
            out = v[0];
        if (ast_node(b->ast, out.node))
            out.span = ast_node(b->ast, out.node)->span;
        break;
    case LL1_NT_FATOR:
        if (rule.rhs_length == 1)
            out.node = literal(b, v[0]);
        else if (v[0].token.kind == TOK_LPAREN) {
            out.node = v[1].node;
            /* Internal builder alone may widen the node span before publication. */
            AstNode *n = (AstNode *)ast_node(b->ast, out.node);
            if (n)
                n->span = span;
        } else if (v[0].token.kind == TOK_NOT || v[0].token.kind == TOK_PLUS ||
                   v[0].token.kind == TOK_MINUS) {
            AstUnaryOperator op = v[0].token.kind == TOK_NOT    ? AST_NOT
                                  : v[0].token.kind == TOK_PLUS ? AST_POSITIVE
                                                                : AST_NEGATIVE;
            out.node = emit(
                b,
                (AstNode){.kind = AST_UNARY, .span = span, .as.unary = {op, v[1].node, v[0].span}});
        } else
            out = v[1];
        break;
    case LL1_NT_EXPR_OU2:
    case LL1_NT_EXPR_E2:
        out = prepend(b,
                      (Link){.node = v[1].node,
                             .op = rule.lhs == LL1_NT_EXPR_OU2 ? AST_OR : AST_AND,
                             .operator_span = v[0].span},
                      v[2]);
        out.span = span;
        break;
    case LL1_NT_EXPR_ADITIVA2:
    case LL1_NT_EXPR_MULTIPLICATIVA2:
        out =
            prepend(b, (Link){.node = v[1].node, .op = v[0].op, .operator_span = v[0].span}, v[2]);
        out.span = span;
        break;
    case LL1_NT_EXPR_RELACIONAL2:
        out = prepend(
            b,
            (Link){.node = v[1].node, .op = comparison(b, v[0].token), .operator_span = v[0].span},
            empty());
        out.span = span;
        break;
    case LL1_NT_EXPR_MULTIPLICATIVA:
    case LL1_NT_EXPR_ADITIVA:
    case LL1_NT_EXPR_RELACIONAL:
    case LL1_NT_EXPR_E:
    case LL1_NT_EXPR_OU:
        out.node = fold(b, v[0].node, v[1]);
        if (ast_node(b->ast, out.node))
            out.span = ast_node(b->ast, out.node)->span;
        break;
    case LL1_NT_EXPRESSAO:
    case LL1_NT_COMPLEMENTO:
    case LL1_NT_CONDICAO_FOR:
        out = v[0];
        break;
    case LL1_NT_NOVO_ARGUMENTO:
    case LL1_NT_NOVOTERMOLEITURA:
    case LL1_NT_NOVOTERMOESCRITA:
        out = prepend(b, (Link){.node = v[1].node}, v[2]);
        out.span = span;
        break;
    case LL1_NT_ARGUMENTOS:
        out = prepend(b, (Link){.node = v[0].node}, v[1]);
        out.span = span;
        break;
    case LL1_NT_FUNCAO:
        out.node = emit(
            b, (AstNode){.kind = AST_CALL, .span = span, .as.call = {name(v[1]), list(b, v[3])}});
        break;
    case LL1_NT_ATRIBUICAO:
    case LL1_NT_INICIALIZACAO_FOR:
    case LL1_NT_ATUALIZACAO_FOR: {
        NodeId target = identifier(b, v[0], empty());
        out.node =
            emit(b, (AstNode){.kind = AST_ASSIGN, .span = span, .as.assign = {target, v[2].node}});
        break;
    }
    case LL1_NT_RETORNO:
        out.node = emit(
            b, (AstNode){.kind = AST_RETURN_STMT, .span = span, .as.return_stmt = {v[1].node}});
        break;
    case LL1_NT_LEITURA:
    case LL1_NT_ESCRITA: {
        Value items = prepend(b, (Link){.node = v[2].node}, v[3]);
        out.node =
            emit(b, (AstNode){.kind = rule.lhs == LL1_NT_LEITURA ? AST_READ_STMT : AST_WRITE_STMT,
                              .span = span,
                              .as.io = {list(b, items)}});
        break;
    }
    case LL1_NT_SENAO:
        out = v[1];
        break;
    case LL1_NT_SELECAO:
        out.node = emit(
            b, (AstNode){.kind = AST_IF_STMT,
                         .span = v[5].node == AST_NODE_NONE ? join(v[0].span, v[4].span) : span,
                         .as.if_stmt = {v[2].node, v[4].node, v[5].node}});
        break;
    case LL1_NT_ENQUANTO:
        out.node = emit(b, (AstNode){.kind = AST_WHILE_STMT,
                                     .span = span,
                                     .as.while_stmt = {v[2].node, v[4].node}});
        break;
    case LL1_NT_PARA:
        out.node = emit(b, (AstNode){.kind = AST_FOR_STMT,
                                     .span = span,
                                     .as.for_stmt = {v[2].node, v[4].node, v[6].node, v[8].node}});
        break;
    case LL1_NT_COMANDO:
        out = v[0];
        if (ast_node(b->ast, out.node) && ast_node(b->ast, out.node)->kind == AST_CALL)
            out.node =
                emit(b, (AstNode){.kind = AST_CALL_STMT, .span = span, .as.call_stmt = {out.node}});
        break;
    case LL1_NT_COMANDOS:
    case LL1_NT_LISTAFUNCOES:
        out = prepend(b, (Link){.node = v[0].node}, v[1]);
        out.span = span;
        break;
    case LL1_NT_LISTAVARIAVEIS: {
        Value ids = prepend(b, (Link){.name = name(v[1])}, v[2]);
        /* Preserve source order while appending the following declaration list. */
        Link *first = NULL, **tail = &first;
        for (Link *p = ids.list; p && ast_status(b->ast) == PARSE_OK; p = p->next) {
            Value id = empty();
            id.token = (Token){.begin = p->name.text.data, .length = p->name.text.length};
            id.span = p->name.span;
            NodeId node = declaration(b, AST_VAR_DECL, v[0].type, id);
            Link *entry = ast_alloc(b->ast, sizeof(*entry));
            if (!entry)
                break;
            *entry = (Link){.node = node};
            *tail = entry;
            tail = &entry->next;
        }
        *tail = v[4].list;
        out.list = first;
        out.count = ids.count + v[4].count;
        break;
    }
    case LL1_NT_LISTAPARAMETROS:
        out = prepend(b, (Link){.node = declaration(b, AST_PARAM_DECL, v[1].type, v[2])}, v[3]);
        out.span = span;
        break;
    case LL1_NT_PARAMETROS:
        out = prepend(b, (Link){.node = declaration(b, AST_PARAM_DECL, v[0].type, v[1])}, v[2]);
        out.span = span;
        break;
    case LL1_NT_BLOCO:
        out.node = emit(
            b,
            (AstNode){.kind = AST_BLOCK, .span = span, .as.block = {list(b, v[1]), list(b, v[2])}});
        break;
    case LL1_NT_PRINCIPAL:
        out.node = emit(b, (AstNode){.kind = AST_MAIN, .span = span, .as.main = {v[3].node}});
        break;
    case LL1_NT_DECFUNCAO:
        out.node =
            emit(b, (AstNode){.kind = AST_FUNCTION_DECL,
                              .span = span,
                              .as.function = {name(v[1]), v[0].type, list(b, v[3]), v[5].node}});
        break;
    case LL1_NT_PROGRAMA:
        out.node = emit(
            b,
            (AstNode){.kind = AST_PROGRAM, .span = span, .as.program = {list(b, v[0]), v[1].node}});
        /* Empty function lists have no source position. */
        if (!v[0].count) {
            AstNode *n = (AstNode *)ast_node(b->ast, out.node);
            if (n)
                n->span = v[1].span;
        }
        break;
    default:
        ast_fail(b->ast, PARSE_INVALID_INPUT);
        break;
    }
    return out;
}

static bool reserve(ParserStream *p, void **buffer, size_t *capacity, size_t count, size_t width)
{
    AstBuilder *b = p->ast_builder;
    if (count > p->max_stack) {
        ast_fail(b->ast, PARSE_LIMIT_EXCEEDED);
        return false;
    }
    if (count <= *capacity)
        return true;
    size_t next = *capacity ? *capacity * 2 : 32;
    if (next < count)
        next = count;
    if (next > p->max_stack)
        next = p->max_stack;
    if (next > SIZE_MAX / width) {
        ast_fail(b->ast, PARSE_LIMIT_EXCEEDED);
        return false;
    }
    void *data = b->allocator.allocate(b->allocator.context, next * width);
    if (!data) {
        ast_fail(b->ast, PARSE_NO_MEMORY);
        return false;
    }
    if (*capacity)
        memcpy(data, *buffer, *capacity * width);
    if (*buffer)
        b->allocator.release(b->allocator.context, *buffer);
    *buffer = data;
    *capacity = next;
    return true;
}
static bool check(ParserStream *p)
{
    ParseStatus status = ast_status(p->ast_builder->ast);
    if (status == PARSE_OK)
        return true;
    parser_resource_error(p, status, NULL);
    return false;
}
static bool complete(ParserStream *p)
{
    AstBuilder *b = p->ast_builder;
    while (b->depth && ast_status(b->ast) == PARSE_OK) {
        Frame f = b->frames[b->depth - 1];
        LL1Production rule = ll1_productions[f.production];
        if (b->count - f.base != rule.rhs_length)
            break;
        Value result = reduce(b, rule, b->values ? b->values + f.base : NULL);
        --b->depth;
        b->count = f.base;
        if (!b->depth) {
            b->root = result.node;
            break;
        }
        if (!reserve(p, (void **)&b->values, &b->value_capacity, b->count + 1, sizeof(*b->values)))
            break;
        b->values[b->count++] = result;
    }
    return check(p);
}
ParseStatus parser_ast_enable(ParserStream *p, uint32_t source)
{
    AstBuilder *b = p->allocator.allocate(p->allocator.context, sizeof(*b));
    if (!b)
        return PARSE_NO_MEMORY;
    *b = (AstBuilder){.allocator = p->allocator, .source = source, .root = AST_NODE_NONE};
    ParseStatus status = ast_create(&b->ast, &b->allocator);
    if (status != PARSE_OK) {
        p->allocator.release(p->allocator.context, b);
        return status;
    }
    p->ast_builder = b;
    return PARSE_OK;
}
bool parser_ast_expand(ParserStream *p, LL1ProductionId production)
{
    AstBuilder *b = p->ast_builder;
    if (!b)
        return true;
    if (!reserve(p, (void **)&b->frames, &b->frame_capacity, b->depth + 1, sizeof(*b->frames)))
        return check(p);
    b->frames[b->depth++] = (Frame){production, b->count};
    return complete(p);
}
bool parser_ast_token(ParserStream *p, Token token)
{
    AstBuilder *b = p->ast_builder;
    if (!b || token.kind == TOK_EOF)
        return true;
    if (!reserve(p, (void **)&b->values, &b->value_capacity, b->count + 1, sizeof(*b->values)))
        return check(p);
    Value v = empty();
    v.span = (SourceSpan){b->source,    p->index,   p->index + 1, token.line,
                          token.column, token.line, token.column};
    /* Text positions are measured from bytes rather than from node widths. */
    if (!token.begin && token.length) {
        ast_fail(b->ast, PARSE_INVALID_INPUT);
        return check(p);
    }
    for (size_t i = 0; i < token.length; ++i) {
        if (token.begin[i] == '\r' || token.begin[i] == '\n') {
            if (token.begin[i] == '\r' || i == 0 || token.begin[i - 1] != '\r') {
                if (v.span.end_line == UINT32_MAX) {
                    ast_fail(b->ast, PARSE_LIMIT_EXCEEDED);
                    return check(p);
                }
                ++v.span.end_line;
            }
            v.span.end_column = 1;
        } else {
            if (v.span.end_column == UINT32_MAX) {
                ast_fail(b->ast, PARSE_LIMIT_EXCEEDED);
                return check(p);
            }
            ++v.span.end_column;
        }
    }
    v.token = token;
    /* Only names, literals and comparison operators need bytes after feed. */
    if (token.kind == TOK_ID || token.kind == TOK_NUM_INT || token.kind == TOK_NUM_DEC ||
        token.kind == TOK_TEXTO || token.kind == TOK_COMP) {
        AstText text = ast_copy_text(b->ast, (AstText){token.begin, token.length});
        v.token.begin = text.data;
    } else
        v.token.begin = NULL;
    b->values[b->count++] = v;
    return complete(p);
}
void parser_ast_discard(ParserStream *p)
{
    AstBuilder *b = p->ast_builder;
    if (!b)
        return;
    ast_destroy(b->ast);
    if (b->frames)
        b->allocator.release(b->allocator.context, b->frames);
    if (b->values)
        b->allocator.release(b->allocator.context, b->values);
    b->allocator.release(b->allocator.context, b);
    p->ast_builder = NULL;
}
ParseStatus parser_stream_create_ast(ParserStream **out, const ParserConfig *config,
                                     uint32_t source)
{
    ParseStatus status = parser_stream_create(out, config);
    if (status != PARSE_OK)
        return status;
    status = parser_ast_enable(*out, source);
    if (status != PARSE_OK) {
        parser_stream_destroy(*out);
        *out = NULL;
    }
    return status;
}
Ast *parser_stream_take_ast(ParserStream *p)
{
    if (!p || p->flow != PARSER_FINISHED || p->result.status != PARSE_OK || !p->ast_builder)
        return NULL;
    AstBuilder *b = p->ast_builder;
    ParseStatus status = ast_publish(b->ast, b->root);
    if (status != PARSE_OK) {
        parser_resource_error(p, status, NULL);
        parser_ast_discard(p);
        return NULL;
    }
    Ast *ast = b->ast;
    b->ast = NULL;
    parser_ast_discard(p);
    return ast;
}
ParseResult parser_parse_ast(const Token *tokens, size_t count, uint32_t source, Ast **out)
{
    if (out)
        *out = NULL;
    /* Preserve vector contract checks, including tokens beyond premature EOF. */
    ParseResult result = {0};
    if (!parser_validate_tokens(tokens, count, &result))
        return result;
    if (!out) {
        parser_add_error(&result, PARSE_INVALID_INPUT, 0, NULL, 0);
        return result;
    }
    ParserStream *p = NULL;
    ParseStatus status = parser_stream_create_ast(&p, NULL, source);
    if (status != PARSE_OK) {
        parser_add_error(&result, status, 0, NULL, 0);
        return result;
    }
    for (size_t i = 0; i < count; ++i) {
        ParserStep step = parser_stream_feed(p, tokens[i]);
        if (step.flow != PARSER_NEED_TOKEN)
            break;
    }
    parser_stream_finish(p);
    *out = parser_stream_take_ast(p);
    result = *parser_stream_result(p);
    parser_stream_destroy(p);
    return result;
}
