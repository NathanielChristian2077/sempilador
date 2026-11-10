#include "ast_internal.h"
#include <stdlib.h>
#include <string.h>

/* Arena chunks own immutable texts/lists; only the node vector relocates. */
typedef struct Chunk {
    struct Chunk *next;
    size_t used, capacity;
    max_align_t alignment;
    unsigned char bytes[];
} Chunk;
struct Ast {
    ParserAllocator allocator;
    Chunk *chunks;
    AstNode *nodes;
    size_t count, capacity;
    NodeId root;
    ParseStatus status;
    bool frozen;
};
static void *standard_allocate(void *c, size_t n)
{
    (void)c;
    return malloc(n);
}
static void standard_release(void *c, void *p)
{
    (void)c;
    free(p);
}
void ast_fail(Ast *a, ParseStatus status)
{
    if (a && a->status == PARSE_OK)
        a->status = status;
}
ParseStatus ast_create(Ast **out, const ParserAllocator *allocator)
{
    if (!out)
        return PARSE_INVALID_INPUT;
    *out = NULL;
    ParserAllocator mem = allocator ? *allocator : (ParserAllocator){0};
    if ((mem.allocate == NULL) != (mem.release == NULL))
        return PARSE_INVALID_INPUT;
    if (!mem.allocate)
        mem = (ParserAllocator){NULL, standard_allocate, standard_release};
    Ast *a = mem.allocate(mem.context, sizeof(*a));
    if (!a)
        return PARSE_NO_MEMORY;
    *a = (Ast){.allocator = mem, .root = AST_NODE_NONE};
    *out = a;
    return PARSE_OK;
}
void *ast_alloc(Ast *a, size_t n)
{
    if (!a || a->status != PARSE_OK || a->frozen || !n)
        return NULL;
    const size_t align = _Alignof(max_align_t);
    if (n > SIZE_MAX - (align - 1)) {
        ast_fail(a, PARSE_LIMIT_EXCEEDED);
        return NULL;
    }
    n = (n + align - 1) / align * align;
    Chunk *c = a->chunks;
    if (!c || n > c->capacity - c->used) {
        size_t capacity = n > 4096 ? n : 4096;
        if (capacity > SIZE_MAX - sizeof(*c)) {
            ast_fail(a, PARSE_LIMIT_EXCEEDED);
            return NULL;
        }
        c = a->allocator.allocate(a->allocator.context, sizeof(*c) + capacity);
        if (!c) {
            ast_fail(a, PARSE_NO_MEMORY);
            return NULL;
        }
        c->next = a->chunks;
        c->used = 0;
        c->capacity = capacity;
        a->chunks = c;
    }
    void *p = c->bytes + c->used;
    c->used += n;
    return p;
}
AstText ast_copy_text(Ast *a, AstText text)
{
    if (!text.data && text.length) {
        ast_fail(a, PARSE_INVALID_INPUT);
        return (AstText){0};
    }
    if (text.length == SIZE_MAX) {
        ast_fail(a, PARSE_LIMIT_EXCEEDED);
        return (AstText){0};
    }
    char *p = ast_alloc(a, text.length + 1);
    if (!p)
        return (AstText){0};
    if (text.length)
        memcpy(p, text.data, text.length);
    p[text.length] = '\0';
    return (AstText){p, text.length};
}
static AstName copy_name(Ast *a, AstName n)
{
    n.text = ast_copy_text(a, n.text);
    return n;
}
static AstList copy_list(Ast *a, AstList list)
{
    if (!list.count)
        return (AstList){0};
    if (!list.items) {
        ast_fail(a, PARSE_INVALID_INPUT);
        return (AstList){0};
    }
    if (list.count > SIZE_MAX / sizeof(NodeId)) {
        ast_fail(a, PARSE_LIMIT_EXCEEDED);
        return (AstList){0};
    }
    NodeId *ids = ast_alloc(a, list.count * sizeof(*ids));
    if (!ids)
        return (AstList){0};
    memcpy(ids, list.items, list.count * sizeof(*ids));
    return (AstList){ids, list.count};
}
static TypeSyntax copy_type(Ast *a, TypeSyntax type)
{
    if (type.rank) {
        if (!type.dimensions) {
            ast_fail(a, PARSE_INVALID_INPUT);
            return type;
        }
        if (type.rank > SIZE_MAX / sizeof(AstDimension)) {
            ast_fail(a, PARSE_LIMIT_EXCEEDED);
            return type;
        }
        AstDimension *d = ast_alloc(a, type.rank * sizeof(*d));
        if (!d)
            return type;
        for (size_t i = 0; i < type.rank; ++i) {
            d[i] = type.dimensions[i];
            d[i].lexeme = ast_copy_text(a, d[i].lexeme);
        }
        type.dimensions = d;
    }
    return type;
}
NodeId ast_append(Ast *a, const AstNode *input)
{
    if (!a || !input || a->frozen || a->status != PARSE_OK)
        return AST_NODE_NONE;
    if ((unsigned)input->kind >= AST_KIND_COUNT) {
        ast_fail(a, PARSE_INVALID_INPUT);
        return AST_NODE_NONE;
    }
    AstNode n = *input;
    n.id = a->count;
    switch (n.kind) {
    case AST_PROGRAM:
        n.as.program.functions = copy_list(a, n.as.program.functions);
        break;
    case AST_FUNCTION_DECL:
        n.as.function.name = copy_name(a, n.as.function.name);
        n.as.function.return_type = copy_type(a, n.as.function.return_type);
        n.as.function.parameters = copy_list(a, n.as.function.parameters);
        break;
    case AST_PARAM_DECL:
    case AST_VAR_DECL:
        n.as.declaration.name = copy_name(a, n.as.declaration.name);
        n.as.declaration.type = copy_type(a, n.as.declaration.type);
        break;
    case AST_BLOCK:
        n.as.block.declarations = copy_list(a, n.as.block.declarations);
        n.as.block.statements = copy_list(a, n.as.block.statements);
        break;
    case AST_INT_LITERAL:
    case AST_FLOAT_LITERAL:
    case AST_TEXT_LITERAL:
        n.as.literal.lexeme = ast_copy_text(a, n.as.literal.lexeme);
        break;
    case AST_NAME:
        n.as.name = copy_name(a, n.as.name);
        break;
    case AST_INDEX:
        n.as.index.indices = copy_list(a, n.as.index.indices);
        break;
    case AST_CALL:
        n.as.call.callee = copy_name(a, n.as.call.callee);
        n.as.call.arguments = copy_list(a, n.as.call.arguments);
        break;
    case AST_READ_STMT:
    case AST_WRITE_STMT:
        n.as.io.items = copy_list(a, n.as.io.items);
        break;
    default:
        break;
    }
    if (a->status != PARSE_OK)
        return AST_NODE_NONE;
    if (a->count == a->capacity) {
        size_t cap = a->capacity ? a->capacity * 2 : 64;
        if (cap < a->capacity || cap > SIZE_MAX / sizeof(*a->nodes)) {
            ast_fail(a, PARSE_LIMIT_EXCEEDED);
            return AST_NODE_NONE;
        }
        AstNode *nodes = a->allocator.allocate(a->allocator.context, cap * sizeof(*nodes));
        if (!nodes) {
            ast_fail(a, PARSE_NO_MEMORY);
            return AST_NODE_NONE;
        }
        if (a->count)
            memcpy(nodes, a->nodes, a->count * sizeof(*nodes));
        if (a->nodes)
            a->allocator.release(a->allocator.context, a->nodes);
        a->nodes = nodes;
        a->capacity = cap;
    }
    a->nodes[a->count++] = n;
    return n.id;
}
const AstNode *ast_node(const Ast *a, NodeId id)
{
    return a && id < a->count ? &a->nodes[id] : NULL;
}
size_t ast_count(const Ast *a)
{
    return a ? a->count : 0;
}
ParseStatus ast_status(const Ast *a)
{
    return a ? a->status : PARSE_INVALID_INPUT;
}
NodeId ast_root(const Ast *a)
{
    return a ? a->root : AST_NODE_NONE;
}
void ast_destroy(Ast *a)
{
    if (!a)
        return;
    ParserAllocator mem = a->allocator;
    Chunk *c = a->chunks;
    while (c) {
        Chunk *next = c->next;
        mem.release(mem.context, c);
        c = next;
    }
    if (a->nodes)
        mem.release(mem.context, a->nodes);
    mem.release(mem.context, a);
}

#define KIND(k) (UINT32_C(1) << (k))
#define LITERAL (KIND(AST_INT_LITERAL) | KIND(AST_FLOAT_LITERAL) | KIND(AST_TEXT_LITERAL))
#define VALUE (LITERAL | KIND(AST_NAME) | KIND(AST_INDEX))
#define EXPR (VALUE | KIND(AST_UNARY) | KIND(AST_BINARY))
#define STMT                                                                                       \
    (KIND(AST_ASSIGN) | KIND(AST_CALL_STMT) | KIND(AST_READ_STMT) | KIND(AST_WRITE_STMT) |         \
     KIND(AST_RETURN_STMT) | KIND(AST_IF_STMT) | KIND(AST_WHILE_STMT) | KIND(AST_FOR_STMT))
static bool child(const Ast *a, NodeId parent, NodeId id, uint32_t kinds, size_t *parents,
                  bool optional)
{
    if (id == AST_NODE_NONE)
        return optional;
    /* A builder is append-only: backward edges guarantee acyclicity. */
    return id < parent && (KIND(a->nodes[id].kind) & kinds) && ++parents[id] == 1;
}
static bool children(const Ast *a, NodeId parent, AstList list, uint32_t kinds, size_t *parents)
{
    for (size_t i = 0; i < list.count; ++i)
        if (!child(a, parent, list.items[i], kinds, parents, false))
            return false;
    return true;
}
static bool type_ok(TypeSyntax t, bool allow_void)
{
    return (unsigned)t.base <= AST_VOID && (allow_void || t.base != AST_VOID) &&
           (t.base != AST_VOID || t.rank == 0);
}
ParseStatus ast_publish(Ast *a, NodeId root)
{
    if (!a)
        return PARSE_INVALID_INPUT;
    if (a->status != PARSE_OK)
        return a->status;
    if (a->frozen)
        return root == a->root ? PARSE_OK : PARSE_INVALID_INPUT;
    if (root >= a->count || a->nodes[root].kind != AST_PROGRAM || root + 1 != a->count) {
        ast_fail(a, PARSE_INVALID_INPUT);
        return a->status;
    }
    size_t *parents = ast_alloc(a, a->count * sizeof(*parents));
    if (!parents)
        return a->status;
    memset(parents, 0, a->count * sizeof(*parents));
    bool ok = true;
#define C(id, k) child(a, i, (id), (k), parents, false)
#define O(id, k) child(a, i, (id), (k), parents, true)
#define L(list, k) children(a, i, (list), (k), parents)
    for (size_t i = 0; i < a->count && ok; ++i) {
        const AstNode *n = &a->nodes[i];
        switch (n->kind) {
        case AST_PROGRAM:
            ok = L(n->as.program.functions, KIND(AST_FUNCTION_DECL)) &&
                 C(n->as.program.main, KIND(AST_MAIN));
            break;
        case AST_MAIN:
            ok = C(n->as.main.body, KIND(AST_BLOCK));
            break;
        case AST_FUNCTION_DECL:
            ok = type_ok(n->as.function.return_type, true) &&
                 L(n->as.function.parameters, KIND(AST_PARAM_DECL)) &&
                 C(n->as.function.body, KIND(AST_BLOCK));
            break;
        case AST_PARAM_DECL:
        case AST_VAR_DECL:
            ok = type_ok(n->as.declaration.type, false);
            break;
        case AST_BLOCK:
            ok = L(n->as.block.declarations, KIND(AST_VAR_DECL)) && L(n->as.block.statements, STMT);
            break;
        case AST_INDEX:
            ok = C(n->as.index.base, KIND(AST_NAME)) && n->as.index.indices.count &&
                 L(n->as.index.indices, EXPR);
            break;
        case AST_UNARY:
            ok = (unsigned)n->as.unary.op <= AST_NOT && C(n->as.unary.operand, EXPR);
            break;
        case AST_BINARY:
            ok = (unsigned)n->as.binary.op <= AST_NE && C(n->as.binary.left, EXPR) &&
                 C(n->as.binary.right, EXPR);
            break;
        case AST_CALL:
            ok = L(n->as.call.arguments, EXPR);
            break;
        case AST_ASSIGN:
            ok = C(n->as.assign.target, KIND(AST_NAME)) &&
                 C(n->as.assign.value, EXPR | KIND(AST_CALL));
            break;
        case AST_CALL_STMT:
            ok = C(n->as.call_stmt.call, KIND(AST_CALL));
            break;
        case AST_READ_STMT:
            ok = n->as.io.items.count && L(n->as.io.items, KIND(AST_NAME) | KIND(AST_INDEX));
            break;
        case AST_WRITE_STMT:
            ok = n->as.io.items.count && L(n->as.io.items, VALUE);
            break;
        case AST_RETURN_STMT:
            ok = C(n->as.return_stmt.value, EXPR);
            break;
        case AST_IF_STMT:
            ok = C(n->as.if_stmt.condition, EXPR) && C(n->as.if_stmt.then_block, KIND(AST_BLOCK)) &&
                 O(n->as.if_stmt.else_block, KIND(AST_BLOCK));
            break;
        case AST_WHILE_STMT:
            ok = C(n->as.while_stmt.condition, EXPR) && C(n->as.while_stmt.body, KIND(AST_BLOCK));
            break;
        case AST_FOR_STMT:
            ok = O(n->as.for_stmt.initializer, KIND(AST_ASSIGN)) &&
                 O(n->as.for_stmt.condition, EXPR) && O(n->as.for_stmt.update, KIND(AST_ASSIGN)) &&
                 C(n->as.for_stmt.body, KIND(AST_BLOCK));
            if (ok && n->as.for_stmt.initializer != AST_NODE_NONE)
                ok =
                    a->nodes[a->nodes[n->as.for_stmt.initializer].as.assign.value].kind != AST_CALL;
            if (ok && n->as.for_stmt.update != AST_NODE_NONE)
                ok = a->nodes[a->nodes[n->as.for_stmt.update].as.assign.value].kind != AST_CALL;
            break;
        default:
            break;
        }
    }
    for (size_t i = 0; i < root && ok; ++i)
        ok = parents[i] == 1;
#undef C
#undef O
#undef L
    if (!ok)
        ast_fail(a, PARSE_INVALID_INPUT);
    else {
        a->root = root;
        a->frozen = true;
    }
    return a->status;
}
