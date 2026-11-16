#ifndef SEMPILADOR_TYPE_H
#define SEMPILADOR_TYPE_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TYPE_CHAR, TYPE_FLOAT, TYPE_INT, TYPE_BOOLEAN, TYPE_VOID
} TypeBase;

/* Borrowed, immutable view. dimensions must contain rank elements.
 * int[2][3] is {TYPE_INT, 2, {2, 3}}; dimension order is significant.
 * Scalars use rank=0 and dimensions=NULL. Copying a Type does not copy storage.
 */
typedef struct {
    TypeBase base;
    size_t rank;
    const size_t *dimensions;
} Type;

/* Void is valid only as a scalar. Zero dimensions and size overflow are invalid.
 * Widths follow SDT2: char=1, float=8, int=4, boolean=1 (not a target ABI).
 */
bool type_valid(Type type);
bool type_equal(Type left, Type right);
/* No storage size for void. On failure, output arguments remain unchanged. */
bool type_size(Type type, size_t *bytes);
bool type_indexed(Type type, size_t indices, Type *element);
/* snprintf semantics: required bytes excluding NUL, or SIZE_MAX for invalid input.
 * NULL buffer is allowed only when capacity=0. Output may be truncated.
 */
size_t type_format(Type type, char *buffer, size_t capacity);

#endif
