#include "../symbol/symbol.h"
#include "../lexer/lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NAMES 200000
#define REPEAT 5

static double elapsed_ns(clock_t start, size_t operations)
{
    return (double)(clock() - start) * 1e9 / CLOCKS_PER_SEC / (double)operations;
}

static void fail(const char *message)
{
    fprintf(stderr, "symbol_bench: %s\n", message);
    exit(EXIT_FAILURE);
}

int main(void)
{
    /* Names live in one buffer, like lexemes in the source. */
    char *names = malloc((size_t)NAMES * 16);
    size_t *offsets = malloc((size_t)NAMES * sizeof(size_t));
    size_t *lengths = malloc((size_t)NAMES * sizeof(size_t));
    if (names == NULL || offsets == NULL || lengths == NULL)
        fail("memória insuficiente");
    size_t used = 0;
    for (size_t i = 0; i < NAMES; ++i) {
        offsets[i] = used;
        lengths[i] = (size_t)snprintf(names + used, 16, "id_%zu", i * 2654435761u % 1000003u);
        used += lengths[i];
    }

    SymbolTable table;
    if (!symbol_table_init(&table))
        fail("memória insuficiente");

    clock_t start = clock();
    for (size_t i = 0; i < NAMES; ++i) {
        if (symbol_table_insert(&table, names + offsets[i], lengths[i], 1, 1) == NULL)
            fail("inserção falhou");
    }
    printf("inserção nova:      %8.1f ns/op (%zu nomes)\n", elapsed_ns(start, NAMES), table.count);

    start = clock();
    for (size_t r = 0; r < REPEAT; ++r) {
        for (size_t i = 0; i < NAMES; ++i)
            symbol_table_insert(&table, names + offsets[i], lengths[i], 1, 1);
    }
    printf("inserção repetida:  %8.1f ns/op\n", elapsed_ns(start, (size_t)NAMES * REPEAT));

    start = clock();
    size_t found = 0;
    for (size_t r = 0; r < REPEAT; ++r) {
        for (size_t i = 0; i < NAMES; ++i)
            found += symbol_table_lookup(&table, names + offsets[i], lengths[i]) != NULL;
    }
    printf("busca:              %8.1f ns/op\n", elapsed_ns(start, (size_t)NAMES * REPEAT));
    if (found != (size_t)NAMES * REPEAT)
        fail("busca não encontrou todos os nomes");
    symbol_table_free(&table);

    /* Whole path used by the CLI: lexer + table over a generated program. */
    const char line[] = "x1 = x2 + y_3 * contador; ";
    size_t lines = 100000;
    size_t length = (sizeof(line) - 1) * lines;
    char *source = malloc(length);
    if (source == NULL)
        fail("memória insuficiente");
    for (size_t i = 0; i < lines; ++i)
        memcpy(source + i * (sizeof(line) - 1), line, sizeof(line) - 1);

    if (!symbol_table_init(&table))
        fail("memória insuficiente");
    start = clock();
    Lexer lexer;
    lexer_init(&lexer, source, length);
    size_t tokens = 0;
    for (Token token = lexer_next(&lexer); token.kind != TOK_EOF; token = lexer_next(&lexer)) {
        ++tokens;
        if (!symbol_table_add_tokens(&table, &token, 1))
            fail("inserção falhou");
    }
    printf("lexer + tabela:     %8.1f ns/token (%zu tokens, %zu símbolos)\n",
           elapsed_ns(start, tokens), tokens, table.count);

    symbol_table_free(&table);
    free(source);
    free(lengths);
    free(offsets);
    free(names);
    return 0;
}
