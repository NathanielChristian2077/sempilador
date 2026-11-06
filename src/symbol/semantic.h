#ifndef SEMPILADOR_SEMANTIC_SYMBOL_H
#define SEMPILADOR_SEMANTIC_SYMBOL_H

#include "../type/type.h"

#include <stdint.h>

typedef size_t SymbolId;
typedef size_t ScopeId;
#define SYMBOL_ID_NONE SIZE_MAX
#define SCOPE_ID_NONE SIZE_MAX
#define SCOPE_GLOBAL ((ScopeId)0)

typedef enum { SYMBOL_VARIABLE, SYMBOL_PARAMETER, SYMBOL_FUNCTION } SymbolKind;
typedef enum { SYMBOL_OK, SYMBOL_DUPLICATE, SYMBOL_INVALID, SYMBOL_NO_MEMORY } SymbolResult;

/* Declaration input is borrowed only during semantic_declare(). */
typedef struct {
    const char *name;
    size_t length;
    SymbolKind kind;
    Type type;                         /* Return type for a function. */
    const Type *parameters;             /* Ordered parameter types, functions only. */
    size_t parameter_count;
    uint32_t line, column;
} SymbolDeclaration;

/* Table-owned, read-only through semantic_symbol(). IDs survive every mutation;
 * this struct's address need not survive a declaration. Its separately allocated
 * name, dimensions and signature remain valid until semantic_table_free().
 */
typedef struct {
    const char *name;
    size_t length;
    SymbolKind kind;
    Type type;
    const Type *parameters;
    size_t parameter_count;
    ScopeId scope;
    uint32_t line, column;
} SemanticSymbol;

/* Allocators must return malloc-aligned, non-overlapping storage; free accepts NULL.
 * The context must outlive the table. This also permits deterministic OOM tests.
 */
typedef struct {
    void *context;
    void *(*allocate)(void *context, size_t bytes);
    void (*release)(void *context, void *memory);
} SymbolAllocator;

typedef struct SemanticScope SemanticScope;
/* Internal fields: use the API, not direct mutation. Zero-initialize or init first. */
typedef struct {
    SemanticSymbol *symbols;
    size_t count, capacity;
    SemanticScope *scopes;
    size_t scope_count, scope_capacity;
    ScopeId current;
    SymbolAllocator allocator;
} SemanticTable;

/* Initialize a fresh table; free a live table before initializing it again. */
bool semantic_table_init(SemanticTable *table);
bool semantic_table_init_with_allocator(SemanticTable *table, SymbolAllocator allocator);
void semantic_table_free(SemanticTable *table);
ScopeId semantic_current_scope(const SemanticTable *table);
/* Creates a child of the active scope. Exiting restores the parent without
 * freeing symbols needed by the AST. The global scope cannot be exited.
 */
bool semantic_enter_scope(SemanticTable *table, ScopeId *scope);
bool semantic_leave_scope(SemanticTable *table);
/* Only declarations in the current scope conflict. The semantic pass controls
 * any additional shadowing restrictions. A duplicate returns its existing ID;
 * other failures return SYMBOL_ID_NONE. Failed declarations add no symbol.
 */
SymbolResult semantic_declare(SemanticTable *table, const SymbolDeclaration *declaration,
                               SymbolId *symbol);
SymbolId semantic_lookup_current(const SemanticTable *table, const char *name, size_t length);
SymbolId semantic_resolve(const SemanticTable *table, const char *name, size_t length);
const SemanticSymbol *semantic_symbol(const SemanticTable *table, SymbolId symbol);

#endif
