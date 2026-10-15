#include "../symbol/symbol.h"
#include "../lexer/lexer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void test_insert_lookup(void)
{
    SymbolTable table;
    assert(symbol_table_init(&table));
    assert(symbol_table_lookup(&table, "x", 1) == NULL);

    const Symbol *x = symbol_table_insert(&table, "x", 1, 1, 5);
    assert(x != NULL && x->occurrences == 1);
    assert(strcmp(x->name, "x") == 0);

    /* Same content through a different pointer and a later position. */
    char other[] = "xy";
    const Symbol *again = symbol_table_insert(&table, other, 1, 3, 2);
    assert(again == x && again->occurrences == 2);
    assert(again->line == 1 && again->column == 5);

    /* Prefixes are distinct names. */
    const Symbol *xy = symbol_table_insert(&table, other, 2, 4, 1);
    assert(xy != NULL && xy != x && table.count == 2);
    assert(symbol_table_lookup(&table, "xy", 2) == xy);
    assert(symbol_table_lookup(&table, "xyz", 2) == xy);
    assert(symbol_table_lookup(&table, "y", 1) == NULL);

    assert(symbol_table_insert(NULL, "x", 1, 1, 1) == NULL);
    assert(symbol_table_lookup(NULL, "x", 1) == NULL);
    symbol_table_free(&table);
    assert(table.count == 0 && table.symbols == NULL);
    symbol_table_free(&table);
}

static void test_growth(void)
{
    SymbolTable table;
    assert(symbol_table_init(&table));
    char name[32];
    const size_t total = 10000;
    for (size_t round = 0; round < 2; ++round) {
        for (size_t i = 0; i < total; ++i) {
            int length = snprintf(name, sizeof(name), "v%zu", i);
            assert(symbol_table_insert(&table, name, (size_t)length, 1, 1) != NULL);
        }
    }
    assert(table.count == total);
    for (size_t i = 0; i < total; ++i) {
        int length = snprintf(name, sizeof(name), "v%zu", i);
        const Symbol *symbol = symbol_table_lookup(&table, name, (size_t)length);
        assert(symbol != NULL && symbol->occurrences == 2);
        assert(symbol == &table.symbols[i]);
    }
    symbol_table_free(&table);
}

static void test_tokens(void)
{
    const char source[] =
        "int f(int a){return a;}\n"
        "main(){ int x, y; // x no comentário\n"
        "  x = func f(y); println(x, \"y\"); @ }";
    Token tokens[128];
    size_t count = 0;
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));
    do {
        assert(count < COUNT(tokens));
        tokens[count++] = lexer_next(&lexer);
    } while (tokens[count - 1].kind != TOK_EOF);

    SymbolTable table;
    assert(symbol_table_init(&table));
    assert(symbol_table_add_tokens(&table, tokens, count));

    const struct { const char *name; uint32_t line, column; size_t occurrences; } expected[] = {
        {"f", 1, 5, 2}, {"a", 1, 11, 2}, {"x", 2, 13, 3}, {"y", 2, 16, 2}
    };
    assert(table.count == COUNT(expected));
    for (size_t i = 0; i < COUNT(expected); ++i) {
        const Symbol *symbol = &table.symbols[i];
        assert(strcmp(symbol->name, expected[i].name) == 0);
        assert(symbol->line == expected[i].line);
        assert(symbol->column == expected[i].column);
        assert(symbol->occurrences == expected[i].occurrences);
    }
    assert(symbol_table_lookup(&table, "main", 4) == NULL);
    assert(symbol_table_lookup(&table, "int", 3) == NULL);
    assert(symbol_table_add_tokens(&table, NULL, 0));
    assert(!symbol_table_add_tokens(&table, NULL, 1));
    symbol_table_free(&table);
}

static void test_print(void)
{
    SymbolTable table;
    assert(symbol_table_init(&table));
    symbol_table_insert(&table, "contador", 8, 2, 7);
    symbol_table_insert(&table, "contador", 8, 5, 1);
    symbol_table_insert(&table, "i", 1, 3, 9);

    const char *path = "symbol_print_test.txt";
    FILE *stream = fopen(path, "w+");
    assert(stream != NULL);
    symbol_table_print(&table, stream);
    rewind(stream);
    char output[512];
    size_t length = fread(output, 1, sizeof(output) - 1, stream);
    output[length] = '\0';
    fclose(stream);
    remove(path);

    assert(strstr(output, "(2 identificadores)") != NULL);
    char *first = strstr(output, "contador");
    char *second = strstr(output, "    2  i ");
    assert(first != NULL && second != NULL && first < second);
    assert(strstr(first, "2      7            2\n") != NULL);
    symbol_table_free(&table);
}

int main(void)
{
    test_insert_lookup();
    test_growth();
    test_tokens();
    test_print();
    printf("symbol_test: OK\n");
    return 0;
}
