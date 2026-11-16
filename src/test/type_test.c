#include "../type/type.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_scalars(void)
{
    const size_t widths[] = {1, 8, 4, 1};
    const char *names[] = {"char", "float", "int", "boolean", "void"};
    for (unsigned b = TYPE_CHAR; b <= TYPE_VOID; ++b) {
        Type type = {(TypeBase)b, 0, NULL};
        assert(type_valid(type) && type_equal(type, type));
        size_t size = 123;
        assert(type_size(type, &size) == (b != TYPE_VOID));
        assert(size == (b == TYPE_VOID ? 123 : widths[b]));
        assert(!type_size(type, NULL));
        char text[32];
        assert(type_format(type, text, sizeof(text)) == strlen(names[b]));
        assert(strcmp(text, names[b]) == 0);
        for (unsigned c = TYPE_CHAR; c <= TYPE_VOID; ++c)
            assert(type_equal(type, (Type){(TypeBase)c, 0, NULL}) == (b == c));
    }
}

static void test_arrays(void)
{
    size_t dimensions[] = {2, 3};
    Type matrix = {TYPE_INT, 2, dimensions};
    size_t size;
    assert(type_size(matrix, &size) && size == 24);
    size_t same[] = {2, 3}, transposed[] = {3, 2};
    assert(type_equal(matrix, (Type){TYPE_INT, 2, same}));
    assert(!type_equal(matrix, (Type){TYPE_INT, 2, transposed}));
    assert(!type_equal(matrix, (Type){TYPE_FLOAT, 2, same}));
    assert(!type_equal(matrix, (Type){TYPE_INT, 0, NULL}));
    Type row, element;
    assert(type_indexed(matrix, 0, &row) && type_equal(row, matrix));
    assert(type_indexed(matrix, 1, &row));
    assert(row.rank == 1 && row.dimensions[0] == 3);
    assert(type_size(row, &size) && size == 12);
    assert(type_indexed(matrix, 2, &element));
    assert(type_equal(element, (Type){TYPE_INT, 0, NULL}));
    assert(!type_indexed(matrix, 3, &row) && row.rank == 1);
    assert(!type_indexed(matrix, 1, NULL));
    assert(!type_indexed((Type){TYPE_VOID, 0, NULL}, 0, &row));
    char expected[] = "int[2][3]";
    for (size_t capacity = 0; capacity <= sizeof(expected) + 1; ++capacity) {
        char buffer[32];
        memset(buffer, '#', sizeof(buffer));
        size_t written = type_format(matrix, buffer, capacity);
        assert(written == strlen(expected));
        if (capacity != 0) {
            size_t length = written < capacity - 1 ? written : capacity - 1;
            assert(memcmp(buffer, expected, length) == 0);
            assert(buffer[length] == '\0');
        }
        assert(buffer[capacity] == '#');
    }
    assert(type_format(matrix, NULL, 0) == strlen(expected));
    assert(type_format(matrix, NULL, 1) == SIZE_MAX);
}

static void test_invalid_and_limits(void)
{
    size_t zero = 0, overflow = SIZE_MAX, dimensions[] = {SIZE_MAX / 4, 2};
    Type invalid[] = {
        {(TypeBase)-1, 0, NULL}, {(TypeBase)99, 0, NULL},
        {TYPE_INT, 1, NULL}, {TYPE_INT, 0, &zero},
        {TYPE_INT, 1, &zero}, {TYPE_VOID, 1, &overflow},
        {TYPE_INT, 1, &overflow}, {TYPE_INT, 2, dimensions},
        {TYPE_CHAR, SIZE_MAX, &overflow}
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        Type type = invalid[i], unchanged = {TYPE_BOOLEAN, 0, NULL};
        size_t size = 543;
        char buffer[16] = "untouched";
        assert(!type_valid(type));
        assert(!type_equal(type, type));
        assert(!type_size(type, &size) && size == 543);
        assert(!type_indexed(type, 0, &unchanged) && unchanged.base == TYPE_BOOLEAN);
        assert(type_format(type, buffer, sizeof(buffer)) == SIZE_MAX);
        assert(strcmp(buffer, "untouched") == 0);
    }
    size_t maximum = SIZE_MAX / 4;
    Type largest = {TYPE_INT, 1, &maximum};
    size_t bytes;
    assert(type_size(largest, &bytes) && bytes == maximum * 4);
    maximum++;
    assert(!type_valid(largest));
    Type byte_array = {TYPE_CHAR, 1, &overflow};
    assert(type_size(byte_array, &bytes) && bytes == SIZE_MAX);
    char text[128];
    assert(type_format(byte_array, text, sizeof(text)) == strlen(text));
}

int main(void)
{
    test_scalars();
    test_arrays();
    test_invalid_and_limits();
    puts("type_test: OK");
    return 0;
}
