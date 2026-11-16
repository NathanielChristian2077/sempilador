#ifndef SEMPILADOR_CODEGEN_H
#define SEMPILADOR_CODEGEN_H

#include <stdbool.h>
#include <stdio.h>

#include "../semantic/semantic_model.h"

/* Writes textual LLVM IR for a semantically valid program. The AST and model are
 * only read. Returns false for an invalid model, memory exhaustion or a write
 * error; out may then hold partial text. */
bool codegen_emit(const Ast *ast, const SemanticModel *model, FILE *out);

#endif
