#ifndef SEMPILADOR_AST_INTERNAL_H
#define SEMPILADOR_AST_INTERNAL_H
#include "ast.h"
#include <stddef.h>
void *ast_alloc(Ast *ast, size_t bytes);
AstText ast_copy_text(Ast *ast, AstText text);
void ast_fail(Ast *ast, ParseStatus status);
#endif
