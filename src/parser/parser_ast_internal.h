#ifndef SEMPILADOR_PARSER_AST_INTERNAL_H
#define SEMPILADOR_PARSER_AST_INTERNAL_H
#include "parser_internal.h"
#include "ast_internal.h"
typedef struct AstBuilder AstBuilder;
ParseStatus parser_ast_enable(ParserStream *p, uint32_t source);
bool parser_ast_expand(ParserStream *p, LL1ProductionId production);
bool parser_ast_token(ParserStream *p, Token token);
void parser_ast_discard(ParserStream *p);
#endif
