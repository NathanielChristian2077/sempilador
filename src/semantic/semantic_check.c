#include "semantic_internal.h"
#include <string.h>

typedef struct {
    SemanticModel *m;
    size_t depth, scope_count;
    ScopeId scopes[SEMANTIC_MAX_DEPTH + 1];
    SemanticType return_type;
    bool return_type_valid, invalid_declarations;
} Check;
static const AstNode *node(Check *c, NodeId id)
{
    return ast_node(c->m->ast, id);
}
static SemanticInfo *info(Check *c, NodeId id)
{
    return semantic_info(c->m, id);
}
static void report(Check *c, NodeId id, SemanticDiagnosticCode code, NodeId related,
                   SourceSpan span)
{
    semantic_report(c->m, id, code, related, span);
}
static void invalid(Check *c, NodeId id)
{
    SemanticInfo *i = info(c, id);
    i->state = SEM_ERROR;
    if (i->type_state != SEM_NOT_APPLICABLE)
        i->type_state = SEM_ERROR;
    i->constant.kind = SEM_CONSTANT_NONE;
}
static bool enter(Check *c, NodeId id)
{
    if (c->m->stopped)
        return false;
    if (c->depth == SEMANTIC_MAX_DEPTH) {
        invalid(c, id);
        semantic_stop(c->m, SEMANTIC_LIMIT_EXCEEDED);
        return false;
    }
    ++c->depth;
    info(c, id)->scope = semantic_current_scope(&c->m->symbols);
    return true;
}
static bool scalar(SemanticType t, TypeBase base)
{
    return !t.is_string && t.concrete.rank == 0 && t.concrete.base == base;
}
static bool numeric(SemanticType t)
{
    return scalar(t, TYPE_INT) || scalar(t, TYPE_FLOAT);
}
static bool same(SemanticType a, SemanticType b)
{
    return a.is_string == b.is_string && (a.is_string || type_equal(a.concrete, b.concrete));
}
static SemanticType concrete(Type t)
{
    return (SemanticType){.concrete = t};
}
static SemanticType simple(TypeBase base)
{
    return concrete((Type){.base = base});
}
static bool name_equal(AstName a, AstName b)
{
    return a.text.length == b.text.length && !memcmp(a.text.data, b.text.data, a.text.length);
}
static void *allocate(Check *c, size_t bytes)
{
    void *p = c->m->allocator.allocate(c->m->allocator.context, bytes);
    if (!p)
        semantic_stop(c->m, SEMANTIC_NO_MEMORY);
    return p;
}
static bool declared_type(Check *c, NodeId id, TypeSyntax syntax)
{
    SemanticInfo *i = info(c, id);
    Type t = {0};
    switch (syntax.base) {
    case AST_CHAR:
        t.base = TYPE_CHAR;
        break;
    case AST_FLOAT:
        t.base = TYPE_FLOAT;
        break;
    case AST_INT:
        t.base = TYPE_INT;
        break;
    case AST_BOOLEAN:
        t.base = TYPE_BOOLEAN;
        break;
    case AST_VOID:
        t.base = TYPE_VOID;
        break;
    }
    t.rank = syntax.rank;
    if (t.rank) {
        if (t.rank > SIZE_MAX / sizeof(size_t)) {
            semantic_stop(c->m, SEMANTIC_LIMIT_EXCEEDED);
            return false;
        }
        size_t *dims = allocate(c, t.rank * sizeof(*dims));
        if (!dims)
            return false;
        c->m->entries[id].dimensions = dims;
        t.dimensions = dims;
        for (size_t k = 0; k < t.rank; ++k) {
            uint64_t value;
            if (!semantic_integer(syntax.dimensions[k].lexeme, SIZE_MAX, &value) || !value) {
                report(c, id, SEM_DIAG_DIMENSION, AST_NODE_NONE, syntax.dimensions[k].span);
                invalid(c, id);
                c->invalid_declarations = true;
                return false;
            }
            dims[k] = (size_t)value;
        }
    }
    if (!type_valid(t)) {
        report(c, id, SEM_DIAG_DIMENSION, AST_NODE_NONE, syntax.span);
        invalid(c, id);
        c->invalid_declarations = true;
        return false;
    }
    i->type = concrete(t);
    i->type_state = SEM_VALID;
    return true;
}
static void use_owned_type(Check *c, NodeId id, Type type)
{
    SemanticEntry *e = &c->m->entries[id];
    if (e->dimensions)
        c->m->allocator.release(c->m->allocator.context, e->dimensions);
    e->dimensions = NULL;
    e->info.type = concrete(type);
}
static bool declare(Check *c, NodeId id, SymbolDeclaration declaration)
{
    SemanticInfo *i = info(c, id);
    i->scope = semantic_current_scope(&c->m->symbols);
    SymbolId symbol = SYMBOL_ID_NONE;
    SymbolResult result = semantic_declare(&c->m->symbols, &declaration, &symbol);
    if (result == SYMBOL_NO_MEMORY) {
        semantic_stop(c->m, SEMANTIC_NO_MEMORY);
        return false;
    }
    if (result == SYMBOL_INVALID) {
        semantic_stop(c->m, SEMANTIC_INVALID_INPUT);
        return false;
    }
    if (result == SYMBOL_DUPLICATE) {
        c->invalid_declarations = true;
        report(c, id, SEM_DIAG_DUPLICATE, AST_NODE_NONE,
               node(c, id)->kind == AST_FUNCTION_DECL ? node(c, id)->as.function.name.span
                                                      : node(c, id)->as.declaration.name.span);
        return false;
    }
    i->symbol = symbol;
    i->state = SEM_VALID;
    use_owned_type(c, id, semantic_symbol(&c->m->symbols, symbol)->type);
    return true;
}
static void signatures(Check *c, AstList functions)
{
    for (size_t j = 0; j < functions.count && !c->m->stopped; ++j) {
        const AstNode *n = node(c, functions.items[j]);
        SemanticInfo *i = info(c, n->id);
        i->scope = SCOPE_GLOBAL;
        bool valid = declared_type(c, n->id, n->as.function.return_type);
        AstList params = n->as.function.parameters;
        Type *types = NULL;
        if (params.count && !c->m->stopped) {
            if (params.count > SIZE_MAX / sizeof(*types)) {
                semantic_stop(c->m, SEMANTIC_LIMIT_EXCEEDED);
                break;
            }
            types = allocate(c, params.count * sizeof(*types));
        }
        for (size_t k = 0; k < params.count && !c->m->stopped; ++k) {
            const AstNode *p = node(c, params.items[k]);
            if (!declared_type(c, p->id, p->as.declaration.type))
                valid = false;
            types[k] = info(c, p->id)->type.concrete;
        }
        if (valid && !c->m->stopped) {
            AstName name = n->as.function.name;
            SymbolDeclaration d = {
                name.text.data, name.text.length, SYMBOL_FUNCTION, i->type.concrete,
                types,          params.count,     name.span.line,  name.span.column};
            if (declare(c, n->id, d)) {
                const SemanticSymbol *s = semantic_symbol(&c->m->symbols, i->symbol);
                for (size_t k = 0; k < params.count; ++k)
                    use_owned_type(c, params.items[k], s->parameters[k]);
            }
        } else
            i->state = SEM_ERROR;
        if (types)
            c->m->allocator.release(c->m->allocator.context, types);
    }
}
static AstName declaration_name(const AstNode *n)
{
    return n->kind == AST_FUNCTION_DECL ? n->as.function.name : n->as.declaration.name;
}
/* A rejected declaration still hides outer declarations. Remember its row so
 * dependent uses do not become spurious "undeclared" errors or resolve outside. */
static bool unavailable(Check *c, AstName name, const SemanticSymbol *resolved)
{
    if (!c->invalid_declarations)
        return false;
    size_t resolved_depth = 0;
    if (resolved)
        for (size_t k = 0; k < c->scope_count; ++k)
            if (c->scopes[k] == resolved->scope)
                resolved_depth = k;
    for (size_t j = 0; j < c->m->count; ++j) {
        const AstNode *n = node(c, j);
        const SemanticInfo *i = info(c, j);
        if ((n->kind != AST_FUNCTION_DECL && n->kind != AST_VAR_DECL &&
             n->kind != AST_PARAM_DECL) ||
            i->state != SEM_ERROR || i->symbol != SYMBOL_ID_NONE ||
            !name_equal(name, declaration_name(n)))
            continue;
        for (size_t k = 0; k < c->scope_count; ++k)
            if (c->scopes[k] == i->scope && (!resolved || k > resolved_depth))
                return true;
        if (!resolved && n->kind == AST_FUNCTION_DECL)
            return true;
    }
    return false;
}
static void resolve(Check *c, NodeId id, AstName name, bool function)
{
    SemanticInfo *i = info(c, id);
    SymbolId symbol = semantic_resolve(&c->m->symbols, name.text.data, name.text.length);
    const SemanticSymbol *s = semantic_symbol(&c->m->symbols, symbol);
    if (unavailable(c, name, s)) {
        invalid(c, id);
        return;
    }
    if (!s) {
        report(c, id, SEM_DIAG_UNDECLARED, AST_NODE_NONE, name.span);
        invalid(c, id);
        return;
    }
    i->symbol = symbol;
    if ((s->kind == SYMBOL_FUNCTION) != function) {
        report(c, id, function ? SEM_DIAG_NOT_FUNCTION : SEM_DIAG_NOT_VARIABLE, AST_NODE_NONE,
               name.span);
        invalid(c, id);
        return;
    }
    i->type = concrete(s->type);
    i->type_state = SEM_VALID;
    i->state = SEM_VALID;
}
static void expression(Check *c, NodeId id, bool minimum_magnitude);
static void index_expression(Check *c, const AstNode *n)
{
    expression(c, n->as.index.base, false);
    SemanticInfo *out = info(c, n->id), *base = info(c, n->as.index.base);
    bool valid = base->type_state == SEM_VALID;
    Type t = base->type.concrete;
    out->symbol = base->symbol;
    AstList indices = n->as.index.indices;
    if (valid && (base->type.is_string || indices.count > t.rank)) {
        report(c, n->id, SEM_DIAG_INDEX_RANK, n->as.index.base, n->span);
        valid = false;
    }
    for (size_t j = 0; j < indices.count && !c->m->stopped; ++j) {
        NodeId id = indices.items[j];
        expression(c, id, false);
        const SemanticInfo *ix = info(c, id);
        if (ix->type_state != SEM_VALID) {
            valid = false;
            continue;
        }
        if (!scalar(ix->type, TYPE_INT)) {
            report(c, n->id, SEM_DIAG_INDEX_TYPE, id, node(c, id)->span);
            valid = false;
        } else if (base->type_state == SEM_VALID && j < t.rank &&
                   ix->constant.kind == SEM_CONSTANT_INT &&
                   (ix->constant.as.integer < 0 ||
                    (uint64_t)ix->constant.as.integer >= t.dimensions[j])) {
            report(c, n->id, SEM_DIAG_INDEX_BOUNDS, id, node(c, id)->span);
            valid = false;
        }
    }
    if (valid && type_indexed(t, indices.count, &t)) {
        out->type = concrete(t);
        out->type_state = SEM_VALID;
        out->state = SEM_VALID;
    } else
        invalid(c, n->id);
}
static void call(Check *c, const AstNode *n)
{
    resolve(c, n->id, n->as.call.callee, true);
    SemanticInfo *out = info(c, n->id);
    bool valid = out->type_state == SEM_VALID;
    const SemanticSymbol *fn = valid ? semantic_symbol(&c->m->symbols, out->symbol) : NULL;
    AstList args = n->as.call.arguments;
    if (fn && args.count != fn->parameter_count) {
        report(c, n->id, SEM_DIAG_ARGUMENT_COUNT, AST_NODE_NONE, n->span);
        valid = false;
    }
    for (size_t j = 0; j < args.count && !c->m->stopped; ++j) {
        expression(c, args.items[j], false);
        const SemanticInfo *arg = info(c, args.items[j]);
        if (arg->type_state != SEM_VALID)
            valid = false;
        else if (fn && j < fn->parameter_count && !same(arg->type, concrete(fn->parameters[j]))) {
            report(c, n->id, SEM_DIAG_ARGUMENT_TYPE, args.items[j], node(c, args.items[j])->span);
            valid = false;
        }
    }
    if (!valid)
        invalid(c, n->id);
}
static void binary(Check *c, const AstNode *n)
{
    expression(c, n->as.binary.left, false);
    expression(c, n->as.binary.right, false);
    SemanticInfo *out = info(c, n->id);
    const SemanticInfo *a = info(c, n->as.binary.left), *b = info(c, n->as.binary.right);
    if (a->type_state != SEM_VALID || b->type_state != SEM_VALID) {
        invalid(c, n->id);
        return;
    }
    AstBinaryOperator op = n->as.binary.op;
    bool logical = op == AST_AND || op == AST_OR;
    if (logical ? (!scalar(a->type, TYPE_BOOLEAN) || !scalar(b->type, TYPE_BOOLEAN))
                : (!numeric(a->type) || !numeric(b->type))) {
        report(c, n->id, SEM_DIAG_OPERATOR, AST_NODE_NONE, n->as.binary.operator_span);
        invalid(c, n->id);
        return;
    }
    bool mixed = !logical && (scalar(a->type, TYPE_FLOAT) || scalar(b->type, TYPE_FLOAT));
    out->type = simple(logical || op >= AST_LT ? TYPE_BOOLEAN : mixed ? TYPE_FLOAT : TYPE_INT);
    if (mixed) {
        if (scalar(a->type, TYPE_INT))
            out->left_conversion = SEM_CONVERT_INT_FLOAT;
        if (scalar(b->type, TYPE_INT))
            out->right_conversion = SEM_CONVERT_INT_FLOAT;
    }
    out->state = SEM_VALID;
    out->type_state = SEM_VALID;
    semantic_fold_binary(c->m, n);
}
static void expression(Check *c, NodeId id, bool minimum_magnitude)
{
    if (!enter(c, id))
        return;
    const AstNode *n = node(c, id);
    SemanticInfo *out = info(c, id);
    switch (n->kind) {
    case AST_NAME:
        resolve(c, id, n->as.name, false);
        break;
    case AST_INDEX:
        index_expression(c, n);
        break;
    case AST_CALL:
        call(c, n);
        break;
    case AST_INT_LITERAL: {
        uint64_t value;
        if (!semantic_integer(n->as.literal.lexeme,
                              minimum_magnitude ? UINT64_C(2147483648) : INT32_MAX, &value)) {
            report(c, id, SEM_DIAG_LITERAL_RANGE, AST_NODE_NONE, n->as.literal.literal_span);
            invalid(c, id);
        } else {
            out->type = simple(TYPE_INT);
            out->type_state = SEM_VALID;
            out->state = SEM_VALID;
            out->constant =
                (SemanticConstant){.kind = SEM_CONSTANT_INT, .as.integer = (int64_t)value};
        }
        break;
    }
    case AST_FLOAT_LITERAL: {
        double value;
        SemanticStatus status = semantic_decimal(n->as.literal.lexeme, &value);
        if (status == SEMANTIC_NO_MEMORY) {
            semantic_stop(c->m, status);
            invalid(c, id);
        } else if (status != SEMANTIC_OK) {
            report(c, id, SEM_DIAG_LITERAL_RANGE, AST_NODE_NONE, n->as.literal.literal_span);
            invalid(c, id);
        } else {
            out->type = simple(TYPE_FLOAT);
            out->type_state = SEM_VALID;
            out->state = SEM_VALID;
            out->constant = (SemanticConstant){.kind = SEM_CONSTANT_FLOAT, .as.decimal = value};
        }
        break;
    }
    case AST_TEXT_LITERAL: {
        AstText text = n->as.literal.lexeme;
        if (text.length < 2 || text.data[0] != '"' || text.data[text.length - 1] != '"') {
            semantic_stop(c->m, SEMANTIC_INVALID_INPUT);
            invalid(c, id);
            break;
        }
        out->type = (SemanticType){.is_string = true};
        out->type_state = SEM_VALID;
        out->state = SEM_VALID;
        out->constant = (SemanticConstant){.kind = SEM_CONSTANT_TEXT,
                                           .as.text = {text.data + 1, text.length - 2}};
        break;
    }
    case AST_UNARY: {
        NodeId operand = n->as.unary.operand;
        bool minimum = n->as.unary.op == AST_NEGATIVE && node(c, operand)->kind == AST_INT_LITERAL;
        expression(c, operand, minimum);
        const SemanticInfo *v = info(c, operand);
        if (v->type_state != SEM_VALID)
            invalid(c, id);
        else if (n->as.unary.op == AST_NOT ? !scalar(v->type, TYPE_BOOLEAN) : !numeric(v->type)) {
            report(c, id, SEM_DIAG_OPERATOR, operand, n->as.unary.operator_span);
            invalid(c, id);
        } else {
            out->type = v->type;
            out->type_state = SEM_VALID;
            out->state = SEM_VALID;
            semantic_fold_unary(c->m, n);
        }
        break;
    }
    case AST_BINARY:
        binary(c, n);
        break;
    default:
        semantic_stop(c->m, SEMANTIC_INVALID_INPUT);
        invalid(c, id);
        break;
    }
    --c->depth;
}
static void variable(Check *c, NodeId id, bool parameter)
{
    const AstNode *n = node(c, id);
    SemanticInfo *i = info(c, id);
    i->scope = semantic_current_scope(&c->m->symbols);
    if (!parameter)
        declared_type(c, id, n->as.declaration.type);
    if (c->m->stopped || i->type_state != SEM_VALID)
        return;
    AstName name = n->as.declaration.name;
    SymbolDeclaration d = {.name = name.text.data,
                           .length = name.text.length,
                           .kind = parameter ? SYMBOL_PARAMETER : SYMBOL_VARIABLE,
                           .type = i->type.concrete,
                           .line = name.span.line,
                           .column = name.span.column};
    declare(c, id, d);
}
static void condition(Check *c, NodeId owner, NodeId id)
{
    expression(c, id, false);
    if (info(c, id)->type_state != SEM_VALID)
        info(c, owner)->state = SEM_ERROR;
    else if (!scalar(info(c, id)->type, TYPE_BOOLEAN))
        report(c, owner, SEM_DIAG_CONDITION, id, node(c, id)->span);
}
static bool block(Check *c, NodeId id, AstList parameters, NodeId owner);
static bool statement(Check *c, NodeId id)
{
    if (!enter(c, id))
        return false;
    const AstNode *n = node(c, id);
    SemanticInfo *out = info(c, id);
    out->state = SEM_VALID;
    bool returns = false;
    switch (n->kind) {
    case AST_ASSIGN: {
        NodeId target = n->as.assign.target, value = n->as.assign.value;
        expression(c, target, false);
        expression(c, value, false);
        const SemanticInfo *a = info(c, target), *b = info(c, value);
        if (a->type_state != SEM_VALID || b->type_state != SEM_VALID)
            invalid(c, id);
        else if (!same(a->type, b->type))
            report(c, id, SEM_DIAG_ASSIGNMENT, value, n->span);
        break;
    }
    case AST_CALL_STMT:
        expression(c, n->as.call_stmt.call, false);
        if (info(c, n->as.call_stmt.call)->state == SEM_ERROR)
            invalid(c, id);
        break;
    case AST_READ_STMT:
    case AST_WRITE_STMT: {
        AstList items = n->as.io.items;
        for (size_t j = 0; j < items.count && !c->m->stopped; ++j) {
            NodeId item = items.items[j];
            expression(c, item, false);
            const SemanticInfo *v = info(c, item);
            if (v->type_state != SEM_VALID) {
                invalid(c, id);
                continue;
            }
            bool printable =
                v->type.is_string || (!v->type.concrete.rank && v->type.concrete.base != TYPE_VOID);
            bool readable = !v->type.is_string && printable;
            if (!(n->kind == AST_READ_STMT ? readable : printable))
                report(c, id, n->kind == AST_READ_STMT ? SEM_DIAG_INPUT_TYPE : SEM_DIAG_OUTPUT_TYPE,
                       item, node(c, item)->span);
        }
        break;
    }
    case AST_RETURN_STMT: {
        returns = true;
        NodeId value = n->as.return_stmt.value;
        expression(c, value, false);
        const SemanticInfo *v = info(c, value);
        if (v->type_state != SEM_VALID || !c->return_type_valid)
            invalid(c, id);
        else if (!same(v->type, c->return_type))
            report(c, id, SEM_DIAG_RETURN_TYPE, value, n->span);
        break;
    }
    case AST_IF_STMT: {
        condition(c, id, n->as.if_stmt.condition);
        bool yes = block(c, n->as.if_stmt.then_block, (AstList){0}, AST_NODE_NONE), no = false;
        if (info(c, n->as.if_stmt.then_block)->state == SEM_ERROR)
            invalid(c, id);
        if (n->as.if_stmt.else_block != AST_NODE_NONE) {
            no = block(c, n->as.if_stmt.else_block, (AstList){0}, AST_NODE_NONE);
            if (info(c, n->as.if_stmt.else_block)->state == SEM_ERROR)
                invalid(c, id);
        }
        returns = yes && no;
        break;
    }
    case AST_WHILE_STMT:
        condition(c, id, n->as.while_stmt.condition);
        block(c, n->as.while_stmt.body, (AstList){0}, AST_NODE_NONE);
        if (info(c, n->as.while_stmt.body)->state == SEM_ERROR)
            invalid(c, id);
        break;
    case AST_FOR_STMT: {
        NodeId parts[] = {n->as.for_stmt.initializer, n->as.for_stmt.condition,
                          n->as.for_stmt.update};
        for (size_t j = 0; j < 3 && !c->m->stopped; ++j)
            if (parts[j] != AST_NODE_NONE) {
                if (j == 1)
                    condition(c, id, parts[j]);
                else
                    statement(c, parts[j]);
                if (info(c, parts[j])->state == SEM_ERROR)
                    invalid(c, id);
            }
        block(c, n->as.for_stmt.body, (AstList){0}, AST_NODE_NONE);
        if (info(c, n->as.for_stmt.body)->state == SEM_ERROR)
            invalid(c, id);
        break;
    }
    default:
        semantic_stop(c->m, SEMANTIC_INVALID_INPUT);
        invalid(c, id);
        break;
    }
    --c->depth;
    return returns;
}
static bool block(Check *c, NodeId id, AstList parameters, NodeId owner)
{
    if (!enter(c, id))
        return false;
    SemanticInfo *out = info(c, id);
    out->state = SEM_VALID;
    ScopeId scope;
    if (!semantic_enter_scope(&c->m->symbols, &scope)) {
        semantic_stop(c->m, SEMANTIC_NO_MEMORY);
        --c->depth;
        return false;
    }
    c->scopes[c->scope_count++] = scope;
    out->scope = scope;
    if (owner != AST_NODE_NONE)
        info(c, owner)->scope = scope;
    for (size_t j = 0; j < parameters.count && !c->m->stopped; ++j) {
        variable(c, parameters.items[j], true);
        if (info(c, parameters.items[j])->state == SEM_ERROR)
            invalid(c, id);
    }
    const AstNode *n = node(c, id);
    for (size_t j = 0; j < n->as.block.declarations.count && !c->m->stopped; ++j) {
        NodeId decl = n->as.block.declarations.items[j];
        variable(c, decl, false);
        if (info(c, decl)->state == SEM_ERROR)
            invalid(c, id);
    }
    bool returns = false;
    for (size_t j = 0; j < n->as.block.statements.count && !c->m->stopped; ++j) {
        NodeId stmt = n->as.block.statements.items[j];
        bool exits = statement(c, stmt);
        returns = returns || exits;
        if (info(c, stmt)->state == SEM_ERROR)
            invalid(c, id);
    }
    semantic_leave_scope(&c->m->symbols);
    --c->scope_count;
    --c->depth;
    return returns;
}
void semantic_check(SemanticModel *m)
{
    Check c = {.m = m, .scope_count = 1, .scopes = {SCOPE_GLOBAL}};
    const AstNode *program = ast_node(m->ast, ast_root(m->ast));
    SemanticInfo *root = semantic_info(m, program->id);
    root->state = SEM_VALID;
    root->scope = SCOPE_GLOBAL;
    signatures(&c, program->as.program.functions);
    for (size_t j = 0; j < program->as.program.functions.count && !m->stopped; ++j) {
        const AstNode *fn = node(&c, program->as.program.functions.items[j]);
        SemanticInfo *f = info(&c, fn->id);
        c.return_type = f->type;
        c.return_type_valid = f->type_state == SEM_VALID;
        bool returns = block(&c, fn->as.function.body, fn->as.function.parameters, fn->id);
        if (!m->stopped && c.return_type_valid && !scalar(c.return_type, TYPE_VOID) && !returns)
            report(&c, fn->id, SEM_DIAG_MISSING_RETURN, fn->as.function.body,
                   fn->as.function.name.span);
        if (info(&c, fn->as.function.body)->state == SEM_ERROR)
            f->state = SEM_ERROR;
        if (f->state == SEM_ERROR)
            root->state = SEM_ERROR;
    }
    if (!m->stopped) {
        const AstNode *main = node(&c, program->as.program.main);
        SemanticInfo *entry = info(&c, main->id);
        entry->state = SEM_VALID;
        c.return_type = simple(TYPE_INT);
        c.return_type_valid = true;
        block(&c, main->as.main.body, (AstList){0}, main->id);
        if (info(&c, main->as.main.body)->state == SEM_ERROR) {
            entry->state = SEM_ERROR;
            root->state = SEM_ERROR;
        }
    }
}
