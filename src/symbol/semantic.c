#include "semantic.h"

#include <stdlib.h>
#include <string.h>

struct SemanticScope {
    ScopeId parent;
    SymbolId *slots;  /* ID + 1; zero is an empty slot. */
    size_t slot_count, count;
};

static void *default_allocate(void *context, size_t bytes)
{
    (void)context;
    return malloc(bytes);
}

static void default_release(void *context, void *memory)
{
    (void)context;
    free(memory);
}

static bool initialized(const SemanticTable *table)
{
    return table != NULL && table->scopes != NULL && table->current < table->scope_count;
}

/* Grow by allocating first; failure preserves all existing IDs and contents. */
static bool reserve(SemanticTable *table, void **data, size_t *capacity,
                     size_t needed, size_t element_size)
{
    if (needed <= *capacity) return true;
    size_t next = *capacity == 0 ? 8 : *capacity;
    if (needed > SIZE_MAX / element_size) return false;
    while (next < needed) {
        if (next > SIZE_MAX / 2) { next = needed; break; }
        next *= 2;
    }
    if (next > SIZE_MAX / element_size) next = needed;
    void *grown = table->allocator.allocate(table->allocator.context, next * element_size);
    if (grown == NULL) return false;
    if (*data != NULL && *capacity != 0)
        memcpy(grown, *data, *capacity * element_size);
    table->allocator.release(table->allocator.context, *data);
    *data = grown;
    *capacity = next;
    return true;
}

static size_t hash_name(const char *name, size_t length)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < length; ++i) {
        hash ^= (unsigned char)name[i];
        hash *= UINT64_C(1099511628211);
    }
    return (size_t)hash;
}

static size_t find_slot(const SemanticTable *table, const SemanticScope *scope,
                         const char *name, size_t length)
{
    size_t slot = hash_name(name, length) & (scope->slot_count - 1);
    while (scope->slots[slot] != 0) {
        const SemanticSymbol *symbol = &table->symbols[scope->slots[slot] - 1];
        if (symbol->length == length && memcmp(symbol->name, name, length) == 0)
            break;
        slot = (slot + 1) & (scope->slot_count - 1);
    }
    return slot;
}

static bool reserve_slots(SemanticTable *table, SemanticScope *scope)
{
    if (scope->slot_count != 0 && scope->count < scope->slot_count / 2)
        return true;
    if (scope->slot_count > SIZE_MAX / 2 / sizeof(SymbolId)) return false;
    size_t count = scope->slot_count == 0 ? 8 : scope->slot_count * 2;
    SymbolId *slots = table->allocator.allocate(table->allocator.context, count * sizeof(*slots));
    if (slots == NULL) return false;
    memset(slots, 0, count * sizeof(*slots));
    SemanticScope grown = {scope->parent, slots, count, scope->count};
    for (size_t i = 0; i < scope->slot_count; ++i) {
        if (scope->slots[i] == 0) continue;
        SymbolId id = scope->slots[i] - 1;
        const SemanticSymbol *symbol = &table->symbols[id];
        grown.slots[find_slot(table, &grown, symbol->name, symbol->length)] = id + 1;
    }
    table->allocator.release(table->allocator.context, scope->slots);
    *scope = grown;
    return true;
}

static bool identifier(const char *name, size_t length)
{
    if (name == NULL || length == 0 || length == SIZE_MAX) return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
              (i != 0 && c >= '0' && c <= '9')))
            return false;
    }
    return true;
}

static void release_symbol(SemanticTable *table, SemanticSymbol *symbol)
{
    SymbolAllocator a = table->allocator;
    a.release(a.context, (void *)symbol->name);
    a.release(a.context, (void *)symbol->type.dimensions);
    for (size_t i = 0; i < symbol->parameter_count; ++i)
        a.release(a.context, (void *)symbol->parameters[i].dimensions);
    a.release(a.context, (void *)symbol->parameters);
}

static bool copy_type(SemanticTable *table, Type source, Type *destination)
{
    size_t *dimensions = NULL;
    if (source.rank != 0) {
        dimensions = table->allocator.allocate(table->allocator.context, source.rank * sizeof(*dimensions));
        if (dimensions == NULL) return false;
        memcpy(dimensions, source.dimensions, source.rank * sizeof(*dimensions));
    }
    *destination = (Type){source.base, source.rank, dimensions};
    return true;
}

bool semantic_table_init_with_allocator(SemanticTable *table, SymbolAllocator allocator)
{
    if (table == NULL) return false;
    *table = (SemanticTable){.current = SCOPE_ID_NONE};
    if (allocator.allocate == NULL || allocator.release == NULL) return false;
    table->allocator = allocator;
    void *scopes = NULL;
    if (!reserve(table, &scopes, &table->scope_capacity, 1, sizeof(SemanticScope)))
        return false;
    table->scopes = scopes;
    table->scopes[0] = (SemanticScope){.parent = SCOPE_ID_NONE};
    table->scope_count = 1;
    table->current = SCOPE_GLOBAL;
    return true;
}

bool semantic_table_init(SemanticTable *table)
{
    return semantic_table_init_with_allocator(table,
        (SymbolAllocator){NULL, default_allocate, default_release});
}

void semantic_table_free(SemanticTable *table)
{
    if (table == NULL) return;
    if (table->allocator.release != NULL) {
        for (size_t i = 0; i < table->count; ++i)
            release_symbol(table, &table->symbols[i]);
        for (size_t i = 0; i < table->scope_count; ++i)
            table->allocator.release(table->allocator.context, table->scopes[i].slots);
        table->allocator.release(table->allocator.context, table->symbols);
        table->allocator.release(table->allocator.context, table->scopes);
    }
    *table = (SemanticTable){.current = SCOPE_ID_NONE};
}

ScopeId semantic_current_scope(const SemanticTable *table)
{
    return initialized(table) ? table->current : SCOPE_ID_NONE;
}

bool semantic_enter_scope(SemanticTable *table, ScopeId *scope)
{
    if (scope != NULL) *scope = SCOPE_ID_NONE;
    if (!initialized(table) || table->scope_count == SIZE_MAX) return false;
    void *scopes = table->scopes;
    if (!reserve(table, &scopes, &table->scope_capacity, table->scope_count + 1, sizeof(SemanticScope)))
        return false;
    table->scopes = scopes;
    ScopeId id = table->scope_count++;
    table->scopes[id] = (SemanticScope){.parent = table->current};
    table->current = id;
    if (scope != NULL) *scope = id;
    return true;
}

bool semantic_leave_scope(SemanticTable *table)
{
    if (!initialized(table) || table->current == SCOPE_GLOBAL) return false;
    table->current = table->scopes[table->current].parent;
    return true;
}

static SymbolId lookup(const SemanticTable *table, ScopeId id, const char *name, size_t length)
{
    const SemanticScope *scope = &table->scopes[id];
    if (scope->slot_count == 0) return SYMBOL_ID_NONE;
    SymbolId slot = scope->slots[find_slot(table, scope, name, length)];
    return slot == 0 ? SYMBOL_ID_NONE : slot - 1;
}

SymbolId semantic_lookup_current(const SemanticTable *table, const char *name, size_t length)
{
    if (!initialized(table) || !identifier(name, length)) return SYMBOL_ID_NONE;
    return lookup(table, table->current, name, length);
}

SymbolId semantic_resolve(const SemanticTable *table, const char *name, size_t length)
{
    if (!initialized(table) || !identifier(name, length)) return SYMBOL_ID_NONE;
    for (ScopeId scope = table->current; scope != SCOPE_ID_NONE; scope = table->scopes[scope].parent) {
        SymbolId id = lookup(table, scope, name, length);
        if (id != SYMBOL_ID_NONE) return id;
    }
    return SYMBOL_ID_NONE;
}

const SemanticSymbol *semantic_symbol(const SemanticTable *table, SymbolId symbol)
{
    return initialized(table) && symbol < table->count ? &table->symbols[symbol] : NULL;
}

SymbolResult semantic_declare(SemanticTable *table, const SymbolDeclaration *declaration,
                               SymbolId *symbol)
{
    if (symbol != NULL) *symbol = SYMBOL_ID_NONE;
    if (!initialized(table) || declaration == NULL) return SYMBOL_INVALID;
    SymbolDeclaration d = *declaration;
    if (!identifier(d.name, d.length) || (unsigned)d.kind > SYMBOL_FUNCTION || !type_valid(d.type) ||
        (d.kind != SYMBOL_FUNCTION && (d.type.base == TYPE_VOID || d.parameter_count != 0 || d.parameters != NULL)) ||
        (d.parameter_count == 0 ? d.parameters != NULL : d.parameters == NULL) ||
        d.parameter_count > SIZE_MAX / sizeof(Type))
        return SYMBOL_INVALID;
    for (size_t i = 0; i < d.parameter_count; ++i)
        if (!type_valid(d.parameters[i]) || d.parameters[i].base == TYPE_VOID)
            return SYMBOL_INVALID;
    SymbolId existing = semantic_lookup_current(table, d.name, d.length);
    if (existing != SYMBOL_ID_NONE) {
        if (symbol != NULL) *symbol = existing;
        return SYMBOL_DUPLICATE;
    }
    if (table->count == SIZE_MAX) return SYMBOL_NO_MEMORY;

    /* Clone borrowed input before growing storage; input may refer to this table. */
    SemanticSymbol owned = {.length = d.length, .kind = d.kind, .scope = table->current,
                             .line = d.line, .column = d.column};
    char *name = table->allocator.allocate(table->allocator.context, d.length + 1);
    if (name == NULL) goto no_memory;
    owned.name = name;
    memcpy(name, d.name, d.length);
    name[d.length] = '\0';
    if (!copy_type(table, d.type, &owned.type)) goto no_memory;
    if (d.parameter_count != 0) {
        Type *parameters = table->allocator.allocate(table->allocator.context, d.parameter_count * sizeof(Type));
        if (parameters == NULL) goto no_memory;
        owned.parameters = parameters;
        for (size_t i = 0; i < d.parameter_count; ++i) {
            if (!copy_type(table, d.parameters[i], &parameters[i])) goto no_memory;
            ++owned.parameter_count;
        }
    }
    void *symbols = table->symbols;
    if (!reserve(table, &symbols, &table->capacity, table->count + 1, sizeof(SemanticSymbol)))
        goto no_memory;
    table->symbols = symbols;
    SemanticScope *scope = &table->scopes[table->current];
    if (!reserve_slots(table, scope)) goto no_memory;
    SymbolId id = table->count;
    scope->slots[find_slot(table, scope, owned.name, owned.length)] = id + 1;
    table->symbols[table->count++] = owned;
    ++scope->count;
    if (symbol != NULL) *symbol = id;
    return SYMBOL_OK;
no_memory:
    release_symbol(table, &owned);
    return SYMBOL_NO_MEMORY;
}
