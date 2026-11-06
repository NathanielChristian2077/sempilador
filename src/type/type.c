#include "type.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static size_t base_width(TypeBase base)
{
    switch (base) {
    case TYPE_CHAR: case TYPE_BOOLEAN: return 1;
    case TYPE_INT: return 4;
    case TYPE_FLOAT: return 8;
    default: return 0;
    }
}

bool type_valid(Type type)
{
    if ((unsigned)type.base > TYPE_VOID ||
        (type.rank == 0 ? type.dimensions != NULL : type.dimensions == NULL) ||
        type.rank > SIZE_MAX / sizeof(size_t))
        return false;
    if (type.base == TYPE_VOID)
        return type.rank == 0;
    size_t size = base_width(type.base);
    for (size_t i = 0; i < type.rank; ++i) {
        if (type.dimensions[i] == 0 || type.dimensions[i] > SIZE_MAX / size)
            return false;
        size *= type.dimensions[i];
    }
    return true;
}

bool type_size(Type type, size_t *bytes)
{
    if (bytes == NULL || !type_valid(type) || type.base == TYPE_VOID)
        return false;
    size_t size = base_width(type.base);
    for (size_t i = 0; i < type.rank; ++i)
        size *= type.dimensions[i];
    *bytes = size;
    return true;
}

bool type_equal(Type left, Type right)
{
    if (!type_valid(left) || !type_valid(right) ||
        left.base != right.base || left.rank != right.rank)
        return false;
    for (size_t i = 0; i < left.rank; ++i)
        if (left.dimensions[i] != right.dimensions[i])
            return false;
    return true;
}

bool type_indexed(Type type, size_t indices, Type *element)
{
    if (element == NULL || !type_valid(type) || indices > type.rank || type.base == TYPE_VOID)
        return false;
    type.rank -= indices;
    type.dimensions = type.rank == 0 ? NULL : type.dimensions + indices;
    *element = type;
    return true;
}

size_t type_format(Type type, char *buffer, size_t capacity)
{
    static const char *const names[] = {"char", "float", "int", "boolean", "void"};
    if ((buffer == NULL && capacity != 0) || !type_valid(type))
        return SIZE_MAX;
    const char *name = names[type.base];
    size_t required = strlen(name);
    if (capacity != 0) {
        size_t copy = required < capacity - 1 ? required : capacity - 1;
        memcpy(buffer, name, copy);
        buffer[copy] = '\0';
    }
    for (size_t i = 0; i < type.rank; ++i) {
        char dimension[3 + sizeof(size_t) * 3];
        int written = snprintf(dimension, sizeof(dimension), "[%zu]", type.dimensions[i]);
        if (written < 0 || (size_t)written >= sizeof(dimension) ||
            (size_t)written > SIZE_MAX - required)
            return SIZE_MAX;
        if (required < capacity) {
            size_t available = capacity - required - 1;
            size_t copy = (size_t)written < available ? (size_t)written : available;
            memcpy(buffer + required, dimension, copy);
            buffer[required + copy] = '\0';
        }
        required += (size_t)written;
    }
    return required;
}
