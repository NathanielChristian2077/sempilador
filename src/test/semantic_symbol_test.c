#include "../symbol/semantic.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static SymbolDeclaration variable(const char *name)
{
    return (SymbolDeclaration){.name = name, .length = strlen(name), .kind = SYMBOL_VARIABLE,
                               .type = {TYPE_INT, 0, NULL}, .line = 2, .column = 5};
}

static SymbolId declare(SemanticTable *table, const char *name)
{
    SymbolDeclaration declaration = variable(name);
    SymbolId id;
    assert(semantic_declare(table, &declaration, &id) == SYMBOL_OK);
    assert(id != SYMBOL_ID_NONE);
    return id;
}

static void test_scopes(void)
{
    SemanticTable table;
    assert(semantic_table_init(&table));
    assert(semantic_current_scope(&table) == SCOPE_GLOBAL);
    assert(!semantic_leave_scope(&table));
    assert(semantic_resolve(&table, "x", 1) == SYMBOL_ID_NONE);
    SymbolId global = declare(&table, "x"), duplicate;
    SymbolDeclaration d = variable("x");
    d.type.base = TYPE_FLOAT;
    assert(semantic_declare(&table, &d, &duplicate) == SYMBOL_DUPLICATE);
    assert(duplicate == global && table.count == 1);
    assert(semantic_symbol(&table, global)->type.base == TYPE_INT);
    ScopeId first;
    assert(semantic_enter_scope(&table, &first));
    assert(first != SCOPE_GLOBAL && first != SCOPE_ID_NONE);
    assert(semantic_lookup_current(&table, "x", 1) == SYMBOL_ID_NONE);
    assert(semantic_resolve(&table, "x", 1) == global);
    SymbolId shadow = declare(&table, "x"), local = declare(&table, "local");
    assert(shadow != global && semantic_resolve(&table, "x", 1) == shadow);
    assert(semantic_enter_scope(&table, NULL));
    assert(semantic_resolve(&table, "local", 5) == local);
    assert(semantic_resolve(&table, "x", 1) == shadow);
    assert(semantic_leave_scope(&table) && semantic_current_scope(&table) == first);
    assert(semantic_leave_scope(&table) && semantic_current_scope(&table) == SCOPE_GLOBAL);
    assert(semantic_resolve(&table, "x", 1) == global);
    assert(semantic_resolve(&table, "local", 5) == SYMBOL_ID_NONE);
    /* Exiting a scope hides declarations but retains AST bindings. */
    assert(semantic_symbol(&table, local)->scope == first);
    assert(semantic_symbol(&table, shadow)->scope == first);
    assert(semantic_enter_scope(&table, NULL));
    assert(semantic_resolve(&table, "local", 5) == SYMBOL_ID_NONE);
    assert(declare(&table, "local") != local);
    assert(semantic_symbol(&table, SYMBOL_ID_NONE) == NULL);
    assert(semantic_symbol(&table, table.count) == NULL);
    semantic_table_free(&table);
    semantic_table_free(&table);
    assert(semantic_current_scope(&table) == SCOPE_ID_NONE);
}

static void test_owned_types_and_signatures(void)
{
    SemanticTable table;
    assert(semantic_table_init(&table));
    char name[] = {'m', 'a', 't', 'X'}; /* First three bytes are the identifier. */
    size_t dims[] = {2, 3};
    SymbolDeclaration d = {.name = name, .length = 3, .kind = SYMBOL_VARIABLE,
                            .type = {TYPE_INT, 2, dims}, .line = 7, .column = 9};
    SymbolId matrix;
    assert(semantic_declare(&table, &d, &matrix) == SYMBOL_OK);
    name[0] = 'X'; dims[0] = 9;
    const SemanticSymbol *m = semantic_symbol(&table, matrix);
    assert(strcmp(m->name, "mat") == 0 && m->type.dimensions[0] == 2);
    assert(m->line == 7 && m->column == 9);
    assert(semantic_resolve(&table, "matrix", 3) == matrix);
    assert(semantic_resolve(&table, "matrix", 6) == SYMBOL_ID_NONE);

    size_t argument_dims[] = {5};
    Type parameters[] = {{TYPE_CHAR, 0, NULL}, {TYPE_FLOAT, 1, argument_dims}};
    d = (SymbolDeclaration){.name = "f", .length = 1, .kind = SYMBOL_FUNCTION,
        .type = {TYPE_VOID, 0, NULL}, .parameters = parameters, .parameter_count = 2};
    SymbolId function;
    assert(semantic_declare(&table, &d, &function) == SYMBOL_OK);
    parameters[0].base = TYPE_INT; argument_dims[0] = 99;
    const SemanticSymbol *f = semantic_symbol(&table, function);
    assert(f->type.base == TYPE_VOID && f->parameter_count == 2);
    assert(f->parameters[0].base == TYPE_CHAR);
    assert(f->parameters[1].base == TYPE_FLOAT && f->parameters[1].dimensions[0] == 5);
    assert(semantic_enter_scope(&table, NULL));
    d = variable("arg"); d.kind = SYMBOL_PARAMETER;
    assert(semantic_declare(&table, &d, NULL) == SYMBOL_OK);
    assert(semantic_resolve(&table, "f", 1) == function);
    semantic_table_free(&table);
}

static void test_growth_and_ids(void)
{
    SemanticTable table;
    assert(semantic_table_init(&table));
    SymbolId root = declare(&table, "root");
    enum { TOTAL = 6000 };
    SymbolId ids[TOTAL];
    char name[32];
    for (size_t i = 0; i < TOTAL; ++i) {
        snprintf(name, sizeof(name), "v%zu", i);
        ids[i] = declare(&table, name);
    }
    for (size_t i = 0; i < TOTAL; ++i) {
        snprintf(name, sizeof(name), "v%zu", i);
        assert(semantic_resolve(&table, name, strlen(name)) == ids[i]);
        assert(strcmp(semantic_symbol(&table, ids[i])->name, name) == 0);
    }
    for (size_t i = 0; i < 200; ++i) {
        assert(semantic_enter_scope(&table, NULL));
        assert(semantic_resolve(&table, "root", 4) == root);
        declare(&table, "local");
    }
    for (size_t i = 0; i < 200; ++i) assert(semantic_leave_scope(&table));
    assert(semantic_resolve(&table, "local", 5) == SYMBOL_ID_NONE);
    assert(strcmp(semantic_symbol(&table, root)->name, "root") == 0);
    semantic_table_free(&table);
}

static void test_invalid(void)
{
    SemanticTable table = {0};
    SymbolId id = 123;
    SymbolDeclaration d = variable("x");
    assert(!semantic_table_init(NULL));
    assert(!semantic_enter_scope(NULL, NULL));
    assert(!semantic_leave_scope(&table));
    assert(semantic_declare(&table, &d, &id) == SYMBOL_INVALID && id == SYMBOL_ID_NONE);
    assert(semantic_symbol(&table, 0) == NULL);
    semantic_table_free(&table);
    assert(semantic_table_init(&table));
    assert(semantic_declare(&table, NULL, &id) == SYMBOL_INVALID);
    const char *bad_names[] = {"", "2D", "a b", "a.b"};
    for (size_t i = 0; i < COUNT(bad_names); ++i) {
        d = variable(bad_names[i]);
        assert(semantic_declare(&table, &d, &id) == SYMBOL_INVALID);
    }
    d = variable("x"); d.length = SIZE_MAX;
    assert(semantic_declare(&table, &d, NULL) == SYMBOL_INVALID);
    d = variable("x"); d.name = NULL;
    assert(semantic_declare(&table, &d, NULL) == SYMBOL_INVALID);
    d = variable("x"); d.kind = (SymbolKind)-1;
    assert(semantic_declare(&table, &d, NULL) == SYMBOL_INVALID);
    d = variable("x"); d.type.base = TYPE_VOID;
    assert(semantic_declare(&table, &d, NULL) == SYMBOL_INVALID);
    Type invalid_parameter = {TYPE_VOID, 0, NULL};
    d = variable("f"); d.kind = SYMBOL_FUNCTION;
    d.parameters = &invalid_parameter; d.parameter_count = 1;
    assert(semantic_declare(&table, &d, NULL) == SYMBOL_INVALID);
    d.parameter_count = SIZE_MAX;
    assert(semantic_declare(&table, &d, NULL) == SYMBOL_INVALID);
    d.parameter_count = 0;
    assert(semantic_declare(&table, &d, NULL) == SYMBOL_INVALID);
    d.parameters = NULL; d.parameter_count = 1;
    assert(semantic_declare(&table, &d, NULL) == SYMBOL_INVALID);
    d = variable("x"); d.parameters = &invalid_parameter; d.parameter_count = 1;
    assert(semantic_declare(&table, &d, NULL) == SYMBOL_INVALID);
    assert(table.count == 0);
    assert(semantic_resolve(&table, NULL, 0) == SYMBOL_ID_NONE);
    semantic_table_free(&table);
}

typedef struct { size_t calls, fail_at, live; } FailingAllocator;

static void *failing_allocate(void *context, size_t bytes)
{
    FailingAllocator *a = context;
    if (++a->calls == a->fail_at) return NULL;
    void *p = malloc(bytes);
    assert(p != NULL);
    ++a->live;
    return p;
}

static void failing_release(void *context, void *memory)
{
    FailingAllocator *a = context;
    if (memory != NULL) { assert(a->live > 0); --a->live; }
    free(memory);
}

static size_t allocation_scenario(size_t fail_at)
{
    FailingAllocator a = {.fail_at = fail_at};
    SymbolAllocator allocator = {&a, failing_allocate, failing_release};
    SemanticTable table;
    if (!semantic_table_init_with_allocator(&table, allocator)) {
        semantic_table_free(&table);
        assert(a.live == 0);
        return a.calls;
    }
    SymbolId first = SYMBOL_ID_NONE;
    for (size_t i = 0; i < 30; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "f%zu", i);
        size_t dimensions[] = {2, 3};
        Type parameters[] = {{TYPE_INT, 2, dimensions}, {TYPE_CHAR, 0, NULL},
                              {TYPE_FLOAT, 1, dimensions}};
        SymbolDeclaration d = {.name = name, .length = strlen(name), .kind = SYMBOL_FUNCTION,
            .type = {TYPE_INT, 2, dimensions}, .parameters = parameters, .parameter_count = 3};
        size_t before = table.count;
        SymbolId id;
        SymbolResult result = semantic_declare(&table, &d, &id);
        if (result != SYMBOL_OK) {
            assert(result == SYMBOL_NO_MEMORY && table.count == before && id == SYMBOL_ID_NONE);
            assert(semantic_lookup_current(&table, name, strlen(name)) == SYMBOL_ID_NONE);
            a.fail_at = 0;
            assert(semantic_declare(&table, &d, &id) == SYMBOL_OK);
        }
        if (first == SYMBOL_ID_NONE) first = id;
        assert(strcmp(semantic_symbol(&table, first)->name, "f0") == 0);
        assert(semantic_resolve(&table, name, strlen(name)) == id);
    }
    /* Force the scope vector to grow, failing each possible allocation in turn. */
    for (size_t i = 0; i < 40; ++i) {
        ScopeId previous = semantic_current_scope(&table), child;
        if (!semantic_enter_scope(&table, &child)) {
            assert(child == SCOPE_ID_NONE && semantic_current_scope(&table) == previous);
            a.fail_at = 0;
            assert(semantic_enter_scope(&table, &child));
        }
        assert(semantic_resolve(&table, "f0", 2) == first);
    }
    semantic_table_free(&table);
    assert(a.live == 0);
    return a.calls;
}

static void test_allocation_failures(void)
{
    size_t allocations = allocation_scenario(0);
    for (size_t fail = 1; fail <= allocations; ++fail)
        allocation_scenario(fail);
    SemanticTable table = {0};
    assert(!semantic_table_init_with_allocator(&table, (SymbolAllocator){0}));
    semantic_table_free(&table);
}

int main(void)
{
    test_scopes();
    test_owned_types_and_signatures();
    test_growth_and_ids();
    test_invalid();
    test_allocation_failures();
    puts("semantic_symbol_test: OK (scopes, signatures, stable IDs, every allocation failure)");
    return 0;
}
