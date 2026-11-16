#include "codegen.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t length, capacity;
} Buffer;

/* An instruction operand: a register, constant or global name. */
typedef struct {
    char text[48];
} Value;

typedef struct {
    const Ast *ast;
    const SemanticModel *model;
    const SemanticTable *symbols;
    Buffer globals, allocas, body;
    unsigned temps, labels, strings;
    Type return_type;
    bool failed, terminated;
} Codegen;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static void put(Codegen *cg, Buffer *b, const char *format, ...)
{
    if (cg->failed)
        return;
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    size_t want = b->length + (size_t)needed + 1;
    if (needed < 0 || want < b->length) {
        cg->failed = true;
        va_end(args);
        return;
    }
    if (want > b->capacity) {
        size_t capacity = b->capacity ? b->capacity : 1024;
        while (capacity < want && capacity <= SIZE_MAX / 2)
            capacity *= 2;
        char *data = capacity < want ? NULL : realloc(b->data, capacity);
        if (data == NULL) {
            cg->failed = true;
            va_end(args);
            return;
        }
        b->data = data;
        b->capacity = capacity;
    }
    vsnprintf(b->data + b->length, (size_t)needed + 1, format, args);
    va_end(args);
    b->length += (size_t)needed;
}

#define EMIT(...) put(cg, &cg->body, __VA_ARGS__)

static const AstNode *node(const Codegen *cg, NodeId id)
{
    return ast_node(cg->ast, id);
}

static const SemanticInfo *info(const Codegen *cg, NodeId id)
{
    return semantic_model_info(cg->model, id);
}

static Type type_of(const Codegen *cg, NodeId id)
{
    return info(cg, id)->type.concrete;
}

/* boolean is i1 in memory too; LLVM gives it one byte, matching type_size(). */
static const char *scalar_type(TypeBase base)
{
    switch (base) {
    case TYPE_CHAR: return "i8";
    case TYPE_FLOAT: return "double";
    case TYPE_INT: return "i32";
    case TYPE_BOOLEAN: return "i1";
    case TYPE_VOID: break;
    }
    return "void";
}

/* int[2][3] becomes [2 x [3 x i32]]. */
static void put_type(Codegen *cg, Buffer *b, Type t)
{
    for (size_t i = 0; i < t.rank; ++i)
        put(cg, b, "[%llu x ", (unsigned long long)t.dimensions[i]);
    put(cg, b, "%s", scalar_type(t.base));
    for (size_t i = 0; i < t.rank; ++i)
        put(cg, b, "]");
}

static unsigned long long size_of(Codegen *cg, Type t)
{
    size_t bytes = 0;
    if (!type_size(t, &bytes))
        cg->failed = true;
    return (unsigned long long)bytes;
}

static Value temp(Codegen *cg)
{
    Value v;
    snprintf(v.text, sizeof(v.text), "%%t%u", ++cg->temps);
    return v;
}

static Value variable(SymbolId symbol)
{
    Value v;
    snprintf(v.text, sizeof(v.text), "%%v%llu", (unsigned long long)symbol);
    return v;
}

/* Storage hoisted to the entry block, so loops do not grow the stack. */
static Value local(Codegen *cg, const char *prefix, Type t)
{
    Value v;
    snprintf(v.text, sizeof(v.text), "%%%s%u", prefix, ++cg->temps);
    put(cg, &cg->allocas, "  %s = alloca ", v.text);
    put_type(cg, &cg->allocas, t);
    put(cg, &cg->allocas, "\n");
    return v;
}

static void start_block(Codegen *cg, const char *name, unsigned label)
{
    EMIT("%s.%u:\n", name, label);
    cg->terminated = false;
}

static void branch(Codegen *cg, const char *name, unsigned label)
{
    if (!cg->terminated)
        EMIT("  br label %%%s.%u\n", name, label);
    cg->terminated = true;
}

/* Code after a return still needs a block; it simply has no predecessor. */
static void reopen(Codegen *cg)
{
    if (cg->terminated)
        start_block(cg, "dead", ++cg->labels);
}

/* Private constant holding bytes plus NUL; returns its global name. */
static Value string_global(Codegen *cg, const char *data, size_t length)
{
    Value v;
    snprintf(v.text, sizeof(v.text), "@.str.%u", ++cg->strings);
    put(cg, &cg->globals, "%s = private unnamed_addr constant [%llu x i8] c\"", v.text,
        (unsigned long long)length + 1);
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c >= 0x20 && c < 0x7f && c != '"' && c != '\\')
            put(cg, &cg->globals, "%c", c);
        else
            put(cg, &cg->globals, "\\%02X", c);
    }
    put(cg, &cg->globals, "\\00\"\n");
    return v;
}

static void copy_array(Codegen *cg, const char *to, const char *from, Type t)
{
    EMIT("  call void @llvm.memmove.p0.p0.i64(ptr %s, ptr %s, i64 %llu, i1 false)\n", to, from,
         size_of(cg, t));
}

/* Each declaration starts at zero, also when a loop re-enters its block. */
static void zero(Codegen *cg, const char *address, Type t)
{
    if (t.rank) {
        EMIT("  call void @llvm.memset.p0.i64(ptr %s, i8 0, i64 %llu, i1 false)\n", address,
             size_of(cg, t));
        return;
    }
    const char *value = t.base == TYPE_BOOLEAN ? "false" : t.base == TYPE_FLOAT ? "0.0" : "0";
    EMIT("  store %s %s, ptr %s\n", scalar_type(t.base), value, address);
}

/* Constants folded by the semantic pass are emitted directly. */
static bool constant(const Codegen *cg, NodeId id, Value *out)
{
    const SemanticInfo *i = info(cg, id);
    const SemanticConstant *c = &i->constant;
    if (i->type.is_string || i->type.concrete.rank)
        return false;
    switch (i->type.concrete.base) {
    case TYPE_INT:
        if (c->kind != SEM_CONSTANT_INT)
            return false;
        snprintf(out->text, sizeof(out->text), "%ld", (long)(int32_t)c->as.integer);
        return true;
    case TYPE_FLOAT: {
        if (c->kind != SEM_CONSTANT_FLOAT && c->kind != SEM_CONSTANT_INT)
            return false;
        double d = c->kind == SEM_CONSTANT_FLOAT ? c->as.decimal : (double)c->as.integer;
        uint64_t bits;
        memcpy(&bits, &d, sizeof(bits));
        /* Hexadecimal is LLVM's exact spelling of a double. */
        snprintf(out->text, sizeof(out->text), "0x%016llX", (unsigned long long)bits);
        return true;
    }
    case TYPE_BOOLEAN:
        if (c->kind != SEM_CONSTANT_BOOLEAN)
            return false;
        snprintf(out->text, sizeof(out->text), "%s", c->as.boolean ? "true" : "false");
        return true;
    default:
        return false;
    }
}

static Value value(Codegen *cg, NodeId id);
static Value call(Codegen *cg, NodeId id);

/* Address of a variable, element or sub-array; array calls yield a temporary. */
static Value address(Codegen *cg, NodeId id)
{
    const AstNode *n = node(cg, id);
    if (n->kind == AST_NAME)
        return variable(info(cg, id)->symbol);
    if (n->kind == AST_CALL)
        return call(cg, id);
    if (n->kind != AST_INDEX) {
        cg->failed = true;
        return (Value){{0}};
    }
    Value base = address(cg, n->as.index.base);
    Type t = type_of(cg, n->as.index.base);
    for (size_t j = 0; j < n->as.index.indices.count; ++j) {
        Value index = value(cg, n->as.index.indices.items[j]);
        Value element = temp(cg);
        EMIT("  %s = getelementptr ", element.text);
        put_type(cg, &cg->body, t);
        EMIT(", ptr %s, i32 0, i32 %s\n", base.text, index.text);
        if (!type_indexed(t, 1, &t))
            cg->failed = true;
        base = element;
    }
    return base;
}

/* Scalar results come back as a value; array results as the address of a
 * temporary passed as a hidden first argument. */
static Value call(Codegen *cg, NodeId id)
{
    const AstNode *n = node(cg, id);
    const SemanticSymbol *fn = semantic_symbol(cg->symbols, info(cg, id)->symbol);
    Value result = {{0}};
    if (fn == NULL) {
        cg->failed = true;
        return result;
    }
    Buffer args = {0};
    const char *separator = "";
    if (fn->type.rank) {
        result = local(cg, "ret", fn->type);
        put(cg, &args, "ptr %s", result.text);
        separator = ", ";
    }
    for (size_t j = 0; j < n->as.call.arguments.count; ++j) {
        NodeId arg = n->as.call.arguments.items[j];
        Type t = type_of(cg, arg);
        /* Arrays travel by address; the callee copies them on entry. */
        Value v = t.rank ? address(cg, arg) : value(cg, arg);
        put(cg, &args, "%s%s %s", separator, t.rank ? "ptr" : scalar_type(t.base), v.text);
        separator = ", ";
    }
    const char *data = args.data ? args.data : "";
    int length = (int)fn->length;
    if (fn->type.rank || fn->type.base == TYPE_VOID) {
        EMIT("  call void @f.%.*s(%s)\n", length, fn->name, data);
    } else {
        result = temp(cg);
        EMIT("  %s = call %s @f.%.*s(%s)\n", result.text, scalar_type(fn->type.base), length,
             fn->name, data);
    }
    free(args.data);
    return result;
}

static Value unary(Codegen *cg, const AstNode *n)
{
    Value operand = value(cg, n->as.unary.operand);
    if (n->as.unary.op == AST_POSITIVE)
        return operand;
    Value result = temp(cg);
    if (n->as.unary.op == AST_NOT)
        EMIT("  %s = xor i1 %s, true\n", result.text, operand.text);
    else if (type_of(cg, n->id).base == TYPE_FLOAT)
        EMIT("  %s = fneg double %s\n", result.text, operand.text);
    else
        EMIT("  %s = sub i32 0, %s\n", result.text, operand.text);
    return result;
}

/* Short-circuit through a stack slot, avoiding phi nodes. */
static Value logical(Codegen *cg, const AstNode *n)
{
    bool is_and = n->as.binary.op == AST_AND;
    const char *name = is_and ? "and" : "or";
    unsigned label = ++cg->labels;
    Value slot = local(cg, "logic", (Type){.base = TYPE_BOOLEAN});
    Value left = value(cg, n->as.binary.left);
    EMIT("  store i1 %s, ptr %s\n", left.text, slot.text);
    if (is_and)
        EMIT("  br i1 %s, label %%and.rhs.%u, label %%and.end.%u\n", left.text, label, label);
    else
        EMIT("  br i1 %s, label %%or.end.%u, label %%or.rhs.%u\n", left.text, label, label);
    cg->terminated = true;
    char rhs[16], end[16];
    snprintf(rhs, sizeof(rhs), "%s.rhs", name);
    snprintf(end, sizeof(end), "%s.end", name);
    start_block(cg, rhs, label);
    Value right = value(cg, n->as.binary.right);
    EMIT("  store i1 %s, ptr %s\n", right.text, slot.text);
    branch(cg, end, label);
    start_block(cg, end, label);
    Value result = temp(cg);
    EMIT("  %s = load i1, ptr %s\n", result.text, slot.text);
    return result;
}

static Value to_float(Codegen *cg, Value v, SemanticConversion conversion)
{
    if (conversion != SEM_CONVERT_INT_FLOAT)
        return v;
    Value result = temp(cg);
    EMIT("  %s = sitofp i32 %s to double\n", result.text, v.text);
    return result;
}

static Value binary(Codegen *cg, const AstNode *n)
{
    AstBinaryOperator op = n->as.binary.op;
    if (op == AST_AND || op == AST_OR)
        return logical(cg, n);
    static const char *const integer[] = {
        [AST_ADD] = "add", [AST_SUBTRACT] = "sub", [AST_MULTIPLY] = "mul",
        [AST_DIVIDE] = "sdiv", [AST_REMAINDER] = "srem",
        [AST_LT] = "icmp slt", [AST_LE] = "icmp sle", [AST_GT] = "icmp sgt",
        [AST_GE] = "icmp sge", [AST_EQ] = "icmp eq", [AST_NE] = "icmp ne"
    };
    static const char *const real[] = {
        [AST_ADD] = "fadd", [AST_SUBTRACT] = "fsub", [AST_MULTIPLY] = "fmul",
        [AST_DIVIDE] = "fdiv", [AST_REMAINDER] = "frem",
        [AST_LT] = "fcmp olt", [AST_LE] = "fcmp ole", [AST_GT] = "fcmp ogt",
        [AST_GE] = "fcmp oge", [AST_EQ] = "fcmp oeq", [AST_NE] = "fcmp une"
    };
    const SemanticInfo *i = info(cg, n->id);
    Value left = to_float(cg, value(cg, n->as.binary.left), i->left_conversion);
    Value right = to_float(cg, value(cg, n->as.binary.right), i->right_conversion);
    bool is_float = type_of(cg, n->as.binary.left).base == TYPE_FLOAT ||
                    type_of(cg, n->as.binary.right).base == TYPE_FLOAT;
    Value result = temp(cg);
    EMIT("  %s = %s %s %s, %s\n", result.text, is_float ? real[op] : integer[op],
         is_float ? "double" : "i32", left.text, right.text);
    return result;
}

static Value value(Codegen *cg, NodeId id)
{
    Value v = {{0}};
    if (constant(cg, id, &v))
        return v;
    const AstNode *n = node(cg, id);
    switch (n->kind) {
    case AST_NAME:
    case AST_INDEX: {
        Value from = address(cg, id);
        v = temp(cg);
        EMIT("  %s = load %s, ptr %s\n", v.text, scalar_type(type_of(cg, id).base), from.text);
        return v;
    }
    case AST_CALL: return call(cg, id);
    case AST_UNARY: return unary(cg, n);
    case AST_BINARY: return binary(cg, n);
    default:
        cg->failed = true;
        return v;
    }
}

static void assign(Codegen *cg, const AstNode *n)
{
    NodeId target = n->as.assign.target, source = n->as.assign.value;
    Type t = type_of(cg, target);
    if (t.rank) {
        Value from = address(cg, source);
        Value to = address(cg, target);
        copy_array(cg, to.text, from.text, t);
        return;
    }
    Value v = value(cg, source);
    Value to = address(cg, target);
    EMIT("  store %s %s, ptr %s\n", scalar_type(t.base), v.text, to.text);
}

static void read_item(Codegen *cg, NodeId item)
{
    Type t = type_of(cg, item);
    Value to = address(cg, item);
    switch (t.base) {
    case TYPE_INT:
        EMIT("  call i32 (ptr, ...) @scanf(ptr @.read.int, ptr %s)\n", to.text);
        break;
    case TYPE_FLOAT:
        EMIT("  call i32 (ptr, ...) @scanf(ptr @.read.float, ptr %s)\n", to.text);
        break;
    case TYPE_CHAR:
        EMIT("  call i32 (ptr, ...) @scanf(ptr @.read.char, ptr %s)\n", to.text);
        break;
    case TYPE_BOOLEAN: {
        /* Read an int; any value other than zero is true. */
        Value number = local(cg, "in", (Type){.base = TYPE_INT});
        Value loaded = temp(cg), truth = temp(cg);
        EMIT("  store i32 0, ptr %s\n", number.text);
        EMIT("  call i32 (ptr, ...) @scanf(ptr @.read.int, ptr %s)\n", number.text);
        EMIT("  %s = load i32, ptr %s\n", loaded.text, number.text);
        EMIT("  %s = icmp ne i32 %s, 0\n", truth.text, loaded.text);
        EMIT("  store i1 %s, ptr %s\n", truth.text, to.text);
        break;
    }
    case TYPE_VOID:
        cg->failed = true;
        break;
    }
}

/* One printf per println: the format joins every item, then a newline. */
static void write_items(Codegen *cg, AstList items)
{
    Buffer format = {0}, args = {0};
    for (size_t j = 0; j < items.count; ++j) {
        NodeId item = items.items[j];
        const SemanticInfo *i = info(cg, item);
        if (i->type.is_string) {
            AstText text = i->constant.as.text;
            Value s = string_global(cg, text.data, text.length);
            put(cg, &format, "%%s");
            put(cg, &args, ", ptr %s", s.text);
            continue;
        }
        Value v = value(cg, item);
        switch (i->type.concrete.base) {
        case TYPE_INT:
            put(cg, &format, "%%d");
            put(cg, &args, ", i32 %s", v.text);
            break;
        case TYPE_FLOAT:
            put(cg, &format, "%%g");
            put(cg, &args, ", double %s", v.text);
            break;
        case TYPE_CHAR: {
            /* Variadic calls promote char to int. */
            Value wide = temp(cg);
            EMIT("  %s = sext i8 %s to i32\n", wide.text, v.text);
            put(cg, &format, "%%c");
            put(cg, &args, ", i32 %s", wide.text);
            break;
        }
        case TYPE_BOOLEAN: {
            Value word = temp(cg);
            EMIT("  %s = select i1 %s, ptr @.true, ptr @.false\n", word.text, v.text);
            put(cg, &format, "%%s");
            put(cg, &args, ", ptr %s", word.text);
            break;
        }
        case TYPE_VOID:
            cg->failed = true;
            break;
        }
    }
    put(cg, &format, "\n");
    if (format.data != NULL) {
        Value f = string_global(cg, format.data, format.length);
        EMIT("  call i32 (ptr, ...) @printf(ptr %s%s)\n", f.text, args.data ? args.data : "");
    }
    free(format.data);
    free(args.data);
}

static void return_value(Codegen *cg, NodeId value_id)
{
    Type t = cg->return_type;
    if (t.rank) {
        Value from = address(cg, value_id);
        copy_array(cg, "%result", from.text, t);
        EMIT("  ret void\n");
    } else {
        Value v = value(cg, value_id);
        EMIT("  ret %s %s\n", scalar_type(t.base), v.text);
    }
    cg->terminated = true;
}

static void block(Codegen *cg, NodeId id);
static void statement(Codegen *cg, NodeId id);

static void if_statement(Codegen *cg, const AstNode *n)
{
    unsigned label = ++cg->labels;
    bool has_else = n->as.if_stmt.else_block != AST_NODE_NONE;
    Value condition = value(cg, n->as.if_stmt.condition);
    EMIT("  br i1 %s, label %%if.then.%u, label %%%s.%u\n", condition.text, label,
         has_else ? "if.else" : "if.end", label);
    cg->terminated = true;
    start_block(cg, "if.then", label);
    block(cg, n->as.if_stmt.then_block);
    branch(cg, "if.end", label);
    if (has_else) {
        start_block(cg, "if.else", label);
        block(cg, n->as.if_stmt.else_block);
        branch(cg, "if.end", label);
    }
    start_block(cg, "if.end", label);
}

static void while_statement(Codegen *cg, const AstNode *n)
{
    unsigned label = ++cg->labels;
    branch(cg, "while.cond", label);
    start_block(cg, "while.cond", label);
    Value condition = value(cg, n->as.while_stmt.condition);
    EMIT("  br i1 %s, label %%while.body.%u, label %%while.end.%u\n", condition.text, label,
         label);
    cg->terminated = true;
    start_block(cg, "while.body", label);
    block(cg, n->as.while_stmt.body);
    branch(cg, "while.cond", label);
    start_block(cg, "while.end", label);
}

/* Every part of the for header is optional; no condition loops forever. */
static void for_statement(Codegen *cg, const AstNode *n)
{
    unsigned label = ++cg->labels;
    if (n->as.for_stmt.initializer != AST_NODE_NONE)
        statement(cg, n->as.for_stmt.initializer);
    branch(cg, "for.cond", label);
    start_block(cg, "for.cond", label);
    if (n->as.for_stmt.condition != AST_NODE_NONE) {
        Value condition = value(cg, n->as.for_stmt.condition);
        EMIT("  br i1 %s, label %%for.body.%u, label %%for.end.%u\n", condition.text, label,
             label);
        cg->terminated = true;
    } else {
        branch(cg, "for.body", label);
    }
    start_block(cg, "for.body", label);
    block(cg, n->as.for_stmt.body);
    branch(cg, "for.step", label);
    start_block(cg, "for.step", label);
    if (n->as.for_stmt.update != AST_NODE_NONE)
        statement(cg, n->as.for_stmt.update);
    branch(cg, "for.cond", label);
    start_block(cg, "for.end", label);
}

static void statement(Codegen *cg, NodeId id)
{
    reopen(cg);
    const AstNode *n = node(cg, id);
    switch (n->kind) {
    case AST_ASSIGN: assign(cg, n); break;
    case AST_CALL_STMT: call(cg, n->as.call_stmt.call); break;
    case AST_READ_STMT:
        for (size_t j = 0; j < n->as.io.items.count; ++j)
            read_item(cg, n->as.io.items.items[j]);
        break;
    case AST_WRITE_STMT: write_items(cg, n->as.io.items); break;
    case AST_RETURN_STMT: return_value(cg, n->as.return_stmt.value); break;
    case AST_IF_STMT: if_statement(cg, n); break;
    case AST_WHILE_STMT: while_statement(cg, n); break;
    case AST_FOR_STMT: for_statement(cg, n); break;
    default: cg->failed = true; break;
    }
}

static void declare_variable(Codegen *cg, NodeId id)
{
    const SemanticInfo *i = info(cg, id);
    Value v = variable(i->symbol);
    put(cg, &cg->allocas, "  %s = alloca ", v.text);
    put_type(cg, &cg->allocas, i->type.concrete);
    put(cg, &cg->allocas, "\n");
    reopen(cg);
    zero(cg, v.text, i->type.concrete);
}

static void block(Codegen *cg, NodeId id)
{
    const AstNode *n = node(cg, id);
    for (size_t j = 0; j < n->as.block.declarations.count; ++j)
        declare_variable(cg, n->as.block.declarations.items[j]);
    for (size_t j = 0; j < n->as.block.statements.count; ++j)
        statement(cg, n->as.block.statements.items[j]);
}

static void write_buffer(const Buffer *b, FILE *out)
{
    if (b->length)
        fwrite(b->data, 1, b->length, out);
}

/* fn is a function declaration, or NULL for main. */
static void function(Codegen *cg, const AstNode *fn, NodeId body, FILE *out)
{
    Buffer head = {0};
    cg->allocas.length = 0;
    cg->body.length = 0;
    cg->temps = 0;
    cg->terminated = false;
    if (fn == NULL) {
        cg->return_type = (Type){.base = TYPE_INT};
        put(cg, &head, "define i32 @main() {\n");
    } else {
        const SemanticSymbol *s = semantic_symbol(cg->symbols, info(cg, fn->id)->symbol);
        if (s == NULL) {
            cg->failed = true;
            return;
        }
        cg->return_type = s->type;
        put(cg, &head, "define %s @f.%.*s(", s->type.rank ? "void" : scalar_type(s->type.base),
            (int)s->length, s->name);
        const char *separator = "";
        if (s->type.rank) {
            put(cg, &head, "ptr %%result");
            separator = ", ";
        }
        AstList params = fn->as.function.parameters;
        for (size_t k = 0; k < params.count; ++k) {
            const SemanticInfo *p = info(cg, params.items[k]);
            Type t = p->type.concrete;
            put(cg, &head, "%s%s %%p%llu", separator, t.rank ? "ptr" : scalar_type(t.base),
                (unsigned long long)k);
            separator = ", ";
            /* Parameters live in their own slot; arrays are copied (value semantics). */
            Value v = variable(p->symbol);
            put(cg, &cg->allocas, "  %s = alloca ", v.text);
            put_type(cg, &cg->allocas, t);
            put(cg, &cg->allocas, "\n");
            char incoming[32];
            snprintf(incoming, sizeof(incoming), "%%p%llu", (unsigned long long)k);
            if (t.rank)
                copy_array(cg, v.text, incoming, t);
            else
                EMIT("  store %s %s, ptr %s\n", scalar_type(t.base), incoming, v.text);
        }
        put(cg, &head, ") {\n");
    }
    block(cg, body);
    /* Falling off the end: main returns 0, void returns, others cannot reach here. */
    if (!cg->terminated) {
        if (fn == NULL)
            EMIT("  ret i32 0\n");
        else if (cg->return_type.rank == 0 && cg->return_type.base == TYPE_VOID)
            EMIT("  ret void\n");
        else
            EMIT("  unreachable\n");
    }
    if (!cg->failed) {
        write_buffer(&head, out);
        fputs("entry:\n", out);
        write_buffer(&cg->allocas, out);
        write_buffer(&cg->body, out);
        fputs("}\n\n", out);
    }
    free(head.data);
}

static const char prelude[] =
    "; Gerado pelo sempilador\n"
    "\n"
    "declare i32 @printf(ptr, ...)\n"
    "declare i32 @scanf(ptr, ...)\n"
    "declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1)\n"
    "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)\n"
    "\n"
    "@.read.int = private unnamed_addr constant [3 x i8] c\"%d\\00\"\n"
    "@.read.float = private unnamed_addr constant [4 x i8] c\"%lf\\00\"\n"
    "@.read.char = private unnamed_addr constant [4 x i8] c\" %c\\00\"\n"
    "@.true = private unnamed_addr constant [5 x i8] c\"true\\00\"\n"
    "@.false = private unnamed_addr constant [6 x i8] c\"false\\00\"\n"
    "\n";

bool codegen_emit(const Ast *ast, const SemanticModel *model, FILE *out)
{
    if (ast == NULL || model == NULL || out == NULL || !semantic_model_valid(model))
        return false;
    Codegen cg = {.ast = ast, .model = model, .symbols = semantic_model_symbols(model)};
    const AstNode *program = node(&cg, ast_root(ast));
    if (program == NULL || program->kind != AST_PROGRAM)
        return false;
    fputs(prelude, out);
    AstList functions = program->as.program.functions;
    for (size_t j = 0; j < functions.count && !cg.failed; ++j) {
        const AstNode *fn = node(&cg, functions.items[j]);
        function(&cg, fn, fn->as.function.body, out);
    }
    if (!cg.failed) {
        const AstNode *entry = node(&cg, program->as.program.main);
        function(&cg, NULL, entry->as.main.body, out);
    }
    /* Module-level globals may follow the functions that use them. */
    if (!cg.failed)
        write_buffer(&cg.globals, out);
    free(cg.globals.data);
    free(cg.allocas.data);
    free(cg.body.data);
    return !cg.failed && !ferror(out);
}
