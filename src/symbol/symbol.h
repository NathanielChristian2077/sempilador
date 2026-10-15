#ifndef SYMBOL_H
#define SYMBOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../token/token.h"

typedef struct {
    char *name;
    size_t length;
    uint32_t line;
    uint32_t column;
    size_t occurrences;
} Symbol;

typedef struct {
    Symbol *symbols;
    size_t count;
    size_t capacity;
    size_t *slots;
    size_t slot_count;
} SymbolTable;

bool symbol_table_init(SymbolTable *table);
void symbol_table_free(SymbolTable *table);

const Symbol *symbol_table_lookup(const SymbolTable *table,
                                  const char *name, size_t length);
const Symbol *symbol_table_insert(SymbolTable *table, const char *name,
                                  size_t length, uint32_t line, uint32_t column);
bool symbol_table_add_tokens(SymbolTable *table, const Token *tokens, size_t count);

void symbol_table_print(const SymbolTable *table, FILE *stream);

#endif
