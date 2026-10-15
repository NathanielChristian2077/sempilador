#include "symbol.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_SLOTS 64

/* FNV-1a over the lexeme bytes. */
static size_t hash_name(const char *name, size_t length)
{
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < length; ++i) {
        hash ^= (unsigned char)name[i];
        hash *= 1099511628211ULL;
    }
    return (size_t)hash;
}

/* Return the slot holding the name, or the empty slot where it belongs. */
static size_t find_slot(const SymbolTable *table, const char *name, size_t length)
{
    size_t mask = table->slot_count - 1;
    size_t slot = hash_name(name, length) & mask;
    while (table->slots[slot] != 0) {
        const Symbol *symbol = &table->symbols[table->slots[slot] - 1];
        if (symbol->length == length && memcmp(symbol->name, name, length) == 0)
            return slot;
        slot = (slot + 1) & mask;
    }
    return slot;
}

static bool grow_slots(SymbolTable *table)
{
    if (table->slot_count > SIZE_MAX / 2 / sizeof(size_t))
        return false;
    size_t slot_count = table->slot_count * 2;
    size_t *slots = calloc(slot_count, sizeof(size_t));
    if (slots == NULL)
        return false;
    free(table->slots);
    table->slots = slots;
    table->slot_count = slot_count;
    for (size_t i = 0; i < table->count; ++i) {
        const Symbol *symbol = &table->symbols[i];
        table->slots[find_slot(table, symbol->name, symbol->length)] = i + 1;
    }
    return true;
}

static bool grow_symbols(SymbolTable *table)
{
    size_t capacity = table->capacity == 0 ? INITIAL_SLOTS / 2 : table->capacity * 2;
    if (capacity < table->capacity || capacity > SIZE_MAX / sizeof(Symbol))
        return false;
    Symbol *symbols = realloc(table->symbols, capacity * sizeof(Symbol));
    if (symbols == NULL)
        return false;
    table->symbols = symbols;
    table->capacity = capacity;
    return true;
}

/* Start an empty table; release it with symbol_table_free. */
bool symbol_table_init(SymbolTable *table)
{
    if (table == NULL)
        return false;
    *table = (SymbolTable){0};
    table->slots = calloc(INITIAL_SLOTS, sizeof(size_t));
    if (table->slots == NULL)
        return false;
    table->slot_count = INITIAL_SLOTS;
    return true;
}

void symbol_table_free(SymbolTable *table)
{
    if (table == NULL)
        return;
    for (size_t i = 0; i < table->count; ++i)
        free(table->symbols[i].name);
    free(table->symbols);
    free(table->slots);
    *table = (SymbolTable){0};
}

/* Compare by content and length; the name does not need a terminator. */
const Symbol *symbol_table_lookup(const SymbolTable *table,
                                  const char *name, size_t length)
{
    if (table == NULL || table->slots == NULL || name == NULL)
        return NULL;
    size_t slot = find_slot(table, name, length);
    return table->slots[slot] == 0 ? NULL : &table->symbols[table->slots[slot] - 1];
}

/* Count one occurrence, keeping the first position of each name. */
const Symbol *symbol_table_insert(SymbolTable *table, const char *name,
                                  size_t length, uint32_t line, uint32_t column)
{
    if (table == NULL || table->slots == NULL || name == NULL || length == SIZE_MAX)
        return NULL;
    size_t slot = find_slot(table, name, length);
    if (table->slots[slot] != 0) {
        Symbol *symbol = &table->symbols[table->slots[slot] - 1];
        ++symbol->occurrences;
        return symbol;
    }

    if ((table->count + 1) * 4 > table->slot_count * 3) {
        if (!grow_slots(table))
            return NULL;
        slot = find_slot(table, name, length);
    }
    if (table->count == table->capacity && !grow_symbols(table))
        return NULL;

    char *copy = malloc(length + 1);
    if (copy == NULL)
        return NULL;
    memcpy(copy, name, length);
    copy[length] = '\0';

    Symbol *symbol = &table->symbols[table->count];
    *symbol = (Symbol){
        .name = copy,
        .length = length,
        .line = line,
        .column = column,
        .occurrences = 1
    };
    table->slots[slot] = ++table->count;
    return symbol;
}

/* Register every TOK_ID, independently of the program being valid. */
bool symbol_table_add_tokens(SymbolTable *table, const Token *tokens, size_t count)
{
    if (table == NULL || (tokens == NULL && count > 0))
        return false;
    for (size_t i = 0; i < count; ++i) {
        const Token *token = &tokens[i];
        if (token->kind == TOK_ID &&
            symbol_table_insert(table, token->begin, token->length,
                                token->line, token->column) == NULL)
            return false;
    }
    return true;
}

/* Print the symbols in order of first occurrence. */
void symbol_table_print(const SymbolTable *table, FILE *stream)
{
    if (table == NULL || stream == NULL)
        return;
    fprintf(stream, "Tabela de símbolos (%zu identificadores)\n", table->count);
    fprintf(stream, "%5s  %-24s %6s %6s %12s\n",
            "#", "lexema", "linha", "coluna", "ocorrências");
    for (size_t i = 0; i < table->count; ++i) {
        const Symbol *symbol = &table->symbols[i];
        fprintf(stream, "%5zu  %-24s %6" PRIu32 " %6" PRIu32 " %12zu\n",
                i + 1, symbol->name, symbol->line, symbol->column,
                symbol->occurrences);
    }
}
