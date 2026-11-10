#include "semantic_internal.h"
#include <stdlib.h>

static void *allocate(void *context, size_t bytes)
{
    (void)context;
    return malloc(bytes);
}
static void release(void *context, void *memory)
{
    (void)context;
    free(memory);
}
/* SymbolAllocator permits release(NULL); ParserAllocator does not require it. */
static void *symbol_allocate(void *context, size_t bytes)
{
    SemanticModel *m = context;
    return m->allocator.allocate(m->allocator.context, bytes);
}
static void symbol_release(void *context, void *memory)
{
    SemanticModel *m = context;
    if (memory)
        m->allocator.release(m->allocator.context, memory);
}
void semantic_stop(SemanticModel *m, SemanticStatus status)
{
    if (m->stopped)
        return;
    SemanticResult *r = &m->result;
    r->status = status;
    SemanticDiagnosticCode code = status == SEMANTIC_NO_MEMORY        ? SEM_DIAG_RESOURCE_MEMORY
                                  : status == SEMANTIC_LIMIT_EXCEEDED ? SEM_DIAG_RESOURCE_LIMIT
                                                                      : SEM_DIAG_INVALID_AST;
    r->diagnostics[r->diagnostic_count++] =
        (SemanticDiagnostic){.code = code, .node = AST_NODE_NONE, .related = AST_NODE_NONE};
    if (r->diagnostic_count == SEMANTIC_MAX_DIAGNOSTICS)
        r->limit_reached = true;
    m->stopped = true;
}
SemanticInfo *semantic_info(SemanticModel *m, NodeId id)
{
    return &m->entries[id].info;
}
void semantic_report(SemanticModel *m, NodeId id, SemanticDiagnosticCode code, NodeId related,
                     SourceSpan span)
{
    SemanticInfo *info = semantic_info(m, id);
    info->state = SEM_ERROR;
    if (m->stopped)
        return;
    SemanticResult *r = &m->result;
    r->status = SEMANTIC_ERRORS;
    r->diagnostics[r->diagnostic_count++] = (SemanticDiagnostic){code, id, span, related};
    if (r->diagnostic_count == SEMANTIC_MAX_DIAGNOSTICS) {
        r->limit_reached = true;
        m->stopped = true;
    }
}
SemanticResult semantic_analyze(const Ast *ast, const ParserAllocator *allocator,
                                SemanticModel **out)
{
    SemanticResult invalid = {.status = SEMANTIC_INVALID_INPUT};
    if (!out)
        return invalid;
    *out = NULL;
    if (!ast || ast_status(ast) != PARSE_OK || ast_root(ast) == AST_NODE_NONE)
        return invalid;
    ParserAllocator mem = allocator ? *allocator : (ParserAllocator){0};
    if ((mem.allocate == NULL) != (mem.release == NULL))
        return invalid;
    if (!mem.allocate)
        mem = (ParserAllocator){NULL, allocate, release};
    SemanticModel *m = mem.allocate(mem.context, sizeof(*m));
    if (!m)
        return (SemanticResult){.status = SEMANTIC_NO_MEMORY};
    *m = (SemanticModel){.ast = ast, .allocator = mem, .count = ast_count(ast)};
    *out = m;
    if (m->count > SIZE_MAX / sizeof(*m->entries)) {
        semantic_stop(m, SEMANTIC_LIMIT_EXCEEDED);
        return m->result;
    }
    m->entries = mem.allocate(mem.context, m->count * sizeof(*m->entries));
    if (!m->entries) {
        semantic_stop(m, SEMANTIC_NO_MEMORY);
        return m->result;
    }
    for (size_t i = 0; i < m->count; ++i) {
        AstKind k = ast_node(ast, i)->kind;
        bool typed = k == AST_FUNCTION_DECL || k == AST_PARAM_DECL || k == AST_VAR_DECL ||
                     (k >= AST_INT_LITERAL && k <= AST_CALL);
        m->entries[i] =
            (SemanticEntry){.info = {.state = SEM_UNANALYZED,
                                     .type_state = typed ? SEM_UNANALYZED : SEM_NOT_APPLICABLE,
                                     .symbol = SYMBOL_ID_NONE,
                                     .scope = SCOPE_ID_NONE}};
    }
    SymbolAllocator sa = {m, symbol_allocate, symbol_release};
    if (!semantic_table_init_with_allocator(&m->symbols, sa)) {
        semantic_stop(m, SEMANTIC_NO_MEMORY);
        return m->result;
    }
    semantic_check(m);
    m->result.complete = !m->stopped;
    return m->result;
}
const SemanticResult *semantic_model_result(const SemanticModel *m)
{
    return m ? &m->result : NULL;
}
const SemanticInfo *semantic_model_info(const SemanticModel *m, NodeId id)
{
    return m && m->entries && id < m->count ? &m->entries[id].info : NULL;
}
const SemanticTable *semantic_model_symbols(const SemanticModel *m)
{
    return m ? &m->symbols : NULL;
}
bool semantic_model_valid(const SemanticModel *m)
{
    return m && m->result.complete && m->result.status == SEMANTIC_OK;
}
void semantic_model_destroy(SemanticModel *m)
{
    if (!m)
        return;
    if (m->entries) {
        for (size_t i = 0; i < m->count; ++i)
            if (m->entries[i].dimensions)
                m->allocator.release(m->allocator.context, m->entries[i].dimensions);
        m->allocator.release(m->allocator.context, m->entries);
    }
    semantic_table_free(&m->symbols);
    m->allocator.release(m->allocator.context, m);
}
const char *semantic_diagnostic_message(SemanticDiagnosticCode code)
{
    static const char *const messages[] = {"dimensão inválida ou tamanho excedido",
                                           "declaração duplicada",
                                           "identificador não declarado",
                                           "identificador não é variável ou parâmetro",
                                           "identificador não é função",
                                           "operandos incompatíveis com operador",
                                           "tipos incompatíveis na atribuição",
                                           "condição deve ser boolean",
                                           "quantidade de argumentos incorreta",
                                           "tipo de argumento incompatível",
                                           "tipo de retorno incompatível",
                                           "função pode terminar sem retornar",
                                           "índice deve ser int",
                                           "quantidade de índices incompatível",
                                           "índice constante fora dos limites",
                                           "destino incompatível com scanf",
                                           "tipo incompatível com println",
                                           "literal fora do intervalo",
                                           "divisor constante igual a zero",
                                           "divisão ou resto inteiro fora do intervalo",
                                           "memória insuficiente na análise semântica",
                                           "limite de recursos da análise semântica",
                                           "entrada inválida para análise semântica"};
    return (unsigned)code < sizeof(messages) / sizeof(*messages)
               ? messages[code]
               : "diagnóstico semântico desconhecido";
}
