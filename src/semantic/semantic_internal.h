#ifndef SEMPILADOR_SEMANTIC_INTERNAL_H
#define SEMPILADOR_SEMANTIC_INTERNAL_H
#include "semantic_model.h"

typedef struct {
    SemanticInfo info;
    size_t *dimensions;
} SemanticEntry;
struct SemanticModel {
    const Ast *ast;
    ParserAllocator allocator;
    SemanticTable symbols;
    SemanticEntry *entries;
    size_t count;
    SemanticResult result;
    bool stopped;
};
void semantic_stop(SemanticModel *m, SemanticStatus status);
void semantic_report(SemanticModel *m, NodeId id, SemanticDiagnosticCode code, NodeId related,
                     SourceSpan span);
SemanticInfo *semantic_info(SemanticModel *m, NodeId id);
void semantic_check(SemanticModel *m);
bool semantic_integer(AstText text, uint64_t maximum, uint64_t *out);
SemanticStatus semantic_decimal(AstText text, double *out);
void semantic_fold_unary(SemanticModel *m, const AstNode *n);
void semantic_fold_binary(SemanticModel *m, const AstNode *n);
#endif
