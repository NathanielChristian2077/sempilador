#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "semantic_internal.h"
#include <errno.h>
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>

_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "semantic constants require IEEE binary64 double");
bool semantic_integer(AstText text, uint64_t maximum, uint64_t *out)
{
    if (!text.data || !text.length)
        return false;
    uint64_t value = 0;
    for (size_t i = 0; i < text.length; ++i) {
        unsigned char c = (unsigned char)text.data[i];
        if (c < '0' || c > '9' || value > maximum / 10 ||
            (value == maximum / 10 && (uint64_t)(c - '0') > maximum % 10))
            return false;
        value = value * 10 + (c - '0');
    }
    *out = value;
    return true;
}
SemanticStatus semantic_decimal(AstText text, double *out)
{
    if (!text.data || !text.length)
        return SEMANTIC_ERRORS;
    bool point = false, digit = false;
    for (size_t i = 0; i < text.length; ++i) {
        char c = text.data[i];
        if (c == '.' && !point && i && i + 1 < text.length)
            point = true;
        else if (c >= '0' && c <= '9')
            digit = true;
        else
            return SEMANTIC_ERRORS;
    }
    if (!point || !digit)
        return SEMANTIC_ERRORS;
    /* Never change the process locale. AST text has a copied NUL terminator. */
    locale_t locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!locale)
        return SEMANTIC_NO_MEMORY;
    errno = 0;
    char *end = NULL;
    double value = strtod_l(text.data, &end, locale);
    int error = errno;
    freelocale(locale);
    if (end != text.data + text.length || !isfinite(value) || (error == ERANGE && value == 0))
        return SEMANTIC_ERRORS;
    *out = value;
    return SEMANTIC_OK;
}
void semantic_fold_unary(SemanticModel *m, const AstNode *n)
{
    SemanticInfo *out = semantic_info(m, n->id);
    SemanticConstant v = semantic_info(m, n->as.unary.operand)->constant;
    if (v.kind == SEM_CONSTANT_NONE)
        return;
    if (n->as.unary.op == AST_NOT) {
        v.as.boolean = !v.as.boolean;
    } else if (n->as.unary.op == AST_NEGATIVE) {
        if (v.kind == SEM_CONSTANT_INT) {
            int64_t result = -v.as.integer;
            if (result < INT32_MIN || result > INT32_MAX)
                return;
            v.as.integer = result;
        } else
            v.as.decimal = -v.as.decimal;
    }
    out->constant = v;
}
static double numeric(SemanticConstant c)
{
    return c.kind == SEM_CONSTANT_INT ? (double)c.as.integer : c.as.decimal;
}
void semantic_fold_binary(SemanticModel *m, const AstNode *n)
{
    SemanticInfo *out = semantic_info(m, n->id);
    SemanticConstant a = semantic_info(m, n->as.binary.left)->constant;
    SemanticConstant b = semantic_info(m, n->as.binary.right)->constant;
    AstBinaryOperator op = n->as.binary.op;
    bool division = op == AST_DIVIDE || op == AST_REMAINDER;
    if (division && ((b.kind == SEM_CONSTANT_INT && b.as.integer == 0) ||
                     (b.kind == SEM_CONSTANT_FLOAT && b.as.decimal == 0))) {
        semantic_report(m, n->id, SEM_DIAG_ZERO_DIVISOR, n->as.binary.right,
                        n->as.binary.operator_span);
        out->type_state = SEM_ERROR;
        return;
    }
    if (division && !out->type.is_string && out->type.concrete.base == TYPE_INT &&
        a.kind == SEM_CONSTANT_INT && b.kind == SEM_CONSTANT_INT && a.as.integer == INT32_MIN &&
        b.as.integer == -1) {
        semantic_report(m, n->id, SEM_DIAG_DIVISION_OVERFLOW, AST_NODE_NONE,
                        n->as.binary.operator_span);
        out->type_state = SEM_ERROR;
        return;
    }
    if (a.kind == SEM_CONSTANT_NONE || b.kind == SEM_CONSTANT_NONE)
        return;
    SemanticConstant result = {0};
    if (op == AST_AND || op == AST_OR) {
        result.kind = SEM_CONSTANT_BOOLEAN;
        result.as.boolean =
            op == AST_AND ? (a.as.boolean && b.as.boolean) : (a.as.boolean || b.as.boolean);
    } else if (op >= AST_LT) {
        double x = numeric(a), y = numeric(b);
        result.kind = SEM_CONSTANT_BOOLEAN;
        switch (op) {
        case AST_LT:
            result.as.boolean = x < y;
            break;
        case AST_LE:
            result.as.boolean = x <= y;
            break;
        case AST_GT:
            result.as.boolean = x > y;
            break;
        case AST_GE:
            result.as.boolean = x >= y;
            break;
        case AST_EQ:
            result.as.boolean = x == y;
            break;
        default:
            result.as.boolean = x != y;
            break;
        }
    } else if (out->type.concrete.base == TYPE_INT) {
        int64_t x = a.as.integer, y = b.as.integer, value = 0;
        switch (op) {
        case AST_ADD:
            value = x + y;
            break;
        case AST_SUBTRACT:
            value = x - y;
            break;
        case AST_MULTIPLY:
            value = x * y;
            break;
        case AST_DIVIDE:
            value = x / y;
            break;
        default:
            value = x % y;
            break;
        }
        /* int32 operands fit int64 intermediates, including multiplication. */
        if (value < INT32_MIN || value > INT32_MAX)
            return;
        result.kind = SEM_CONSTANT_INT;
        result.as.integer = value;
    } else {
        double x = numeric(a), y = numeric(b), value = 0;
        switch (op) {
        case AST_ADD:
            value = x + y;
            break;
        case AST_SUBTRACT:
            value = x - y;
            break;
        case AST_MULTIPLY:
            value = x * y;
            break;
        case AST_DIVIDE:
            value = x / y;
            break;
        default:
            value = fmod(x, y);
            break;
        }
        if (!isfinite(value))
            return;
        result.kind = SEM_CONSTANT_FLOAT;
        result.as.decimal = value;
    }
    out->constant = result;
}
