#include "parser_ast_internal.h"

/* Nullable tails defer missing-delimiter diagnostics to their continuation. */
static bool transparent(LL1Nonterminal n)
{
    switch (n) {
    case LL1_NT_DIMENSAO: case LL1_NT_DIMENSAO2: case LL1_NT_LISTAID:
    case LL1_NT_LISTAPARAMETROS: case LL1_NT_LISTAVARIAVEIS:
    case LL1_NT_NOVOTERMOLEITURA: case LL1_NT_NOVOTERMOESCRITA:
    case LL1_NT_NOVO_ARGUMENTO: case LL1_NT_SENAO:
    case LL1_NT_EXPR_OU2: case LL1_NT_EXPR_E2: case LL1_NT_EXPR_RELACIONAL2:
    case LL1_NT_EXPR_ADITIVA2: case LL1_NT_EXPR_MULTIPLICATIVA2:
        return true;
    default: return false;
    }
}

static uint64_t expected(const ParserStream *p, TokenKind found)
{
    for (size_t i = p->size; i > 0; --i) {
        LL1Symbol s = p->stack[i - 1];
        if (s == PARSER_CONTEXT_END) continue;
        if (!(s & LL1_NONTERMINAL_FLAG)) return TOKEN_BIT(s);
        LL1Nonterminal n = (LL1Nonterminal)(s & ~LL1_NONTERMINAL_FLAG);
        if (transparent(n) || (n == LL1_NT_COMANDOS && found == TOK_EOF)) continue;
        return ll1_expected[n];
    }
    return TOKEN_BIT(TOK_EOF);
}

/* This is recovery after recording an error, never an epsilon prediction. */
static LL1Symbol recovery_boundary(ParserStream *p, TokenKind found)
{
    while (p->size) {
        LL1Symbol s = p->stack[p->size - 1];
        if (s == PARSER_CONTEXT_END) {
            --p->size;
            --p->context_count;
            continue;
        }
        if (s & LL1_NONTERMINAL_FLAG) {
            LL1Nonterminal n = (LL1Nonterminal)(s & ~LL1_NONTERMINAL_FLAG);
            if (transparent(n) || (n == LL1_NT_COMANDOS && found == TOK_EOF)) {
                --p->size;
                continue;
            }
        }
        return s;
    }
    return TOK_EOF;
}

static bool expand(ParserStream *p, LL1Nonterminal n, LL1ProductionId id, const Token *token)
{
    if (!parser_ast_expand(p, id)) return false;
    LL1Production rule = ll1_productions[id];
    bool declaration = n == LL1_NT_LISTAVARIAVEIS && rule.rhs_length != 0;
    bool context = declaration || n == LL1_NT_COMANDO || n == LL1_NT_DECFUNCAO;
    size_t needed = p->size - 1 + rule.rhs_length + (context ? 1 : 0);
    if (!parser_reserve(p, needed, p->context_count + (context ? 1 : 0), token)) return false;
    --p->size;
    size_t length = rule.rhs_length;
    if (declaration) p->stack[p->size++] = ll1_rhs[rule.rhs_offset + --length];
    if (context) {
        RecoveryKind kind = declaration ? RECOVER_DECLARATION :
                            n == LL1_NT_COMANDO ? RECOVER_COMMAND : RECOVER_FUNCTION;
        p->contexts[p->context_count++] = (ParserContext){p->size, p->index, p->depth, kind, token->kind};
        p->stack[p->size++] = PARSER_CONTEXT_END;
    }
    while (length != 0) p->stack[p->size++] = ll1_rhs[rule.rhs_offset + --length];
    return true;
}

void parser_process_token(ParserStream *p, Token token)
{
    while (p->flow == PARSER_NEED_TOKEN) {
        if (p->mode != RECOVERY_NONE && parser_recover_token(p, token)) return;
        if (p->flow != PARSER_NEED_TOKEN) return;
        if (p->size == 0) {
            p->flow = PARSER_FINISHED;
            return;
        }
        LL1Symbol top = p->stack[p->size - 1];
        if (top == PARSER_CONTEXT_END) {
            --p->size;
            --p->context_count;
            continue;
        }
        if (top & LL1_NONTERMINAL_FLAG) {
            LL1Nonterminal n = (LL1Nonterminal)(top & ~LL1_NONTERMINAL_FLAG);
            LL1ProductionId id = ll1_table[n][token.kind];
            if (id != LL1_ERROR) {
                if (!expand(p, n, id, &token)) return;
                continue;
            }
        } else if (top == token.kind) {
            if (!parser_ast_token(p, token)) return;
            --p->size;
            parser_track(p, token.kind);
            while (p->size && p->stack[p->size - 1] == PARSER_CONTEXT_END) {
                --p->size;
                --p->context_count;
            }
            if (p->size == 0) p->flow = PARSER_FINISHED;
            return;
        }
        parser_ast_discard(p);
        parser_add_error(&p->result, PARSE_SYNTAX_ERROR, p->index, &token, expected(p, token.kind));
        if (p->result.limit_reached) { p->flow = PARSER_STOPPED; return; }
        if (!parser_begin_recovery(p, &token, recovery_boundary(p, token.kind))) {
            p->flow = token.kind == TOK_EOF ? PARSER_FINISHED : PARSER_STOPPED;
            return;
        }
    }
}

ParserStep parser_stream_feed(ParserStream *p, Token token)
{
    if (p == NULL) return (ParserStep){PARSER_PROTOCOL_ERROR, false};
    if (p->flow != PARSER_NEED_TOKEN) {
        if (p->flow == PARSER_FINISHED) {
            parser_add_error(&p->result, PARSE_INVALID_INPUT, p->next_index, &token, 0);
            p->flow = PARSER_STOPPED;
        }
        return (ParserStep){PARSER_PROTOCOL_ERROR, false};
    }
    p->index = p->next_index;
    if (p->next_index == SIZE_MAX) {
        parser_resource_error(p, PARSE_LIMIT_EXCEEDED, &token);
    } else if ((unsigned)token.kind > TOK_COMMENT || token.kind == TOK_INVALID) {
        parser_add_error(&p->result, token.kind == TOK_INVALID ? PARSE_LEXICAL_ERROR : PARSE_INVALID_INPUT,
                         p->index, &token, 0);
        p->flow = PARSER_STOPPED;
    } else if (token.kind != TOK_COMMENT) {
        parser_process_token(p, token);
    }
    if (p->result.status != PARSE_OK) parser_ast_discard(p);
    if (p->next_index != SIZE_MAX) ++p->next_index;
    return (ParserStep){p->flow, true};
}

const ParseResult *parser_stream_result(const ParserStream *p)
{
    return p == NULL ? NULL : &p->result;
}

ParseResult parser_stream_finish(ParserStream *p)
{
    if (p == NULL) {
        ParseResult result = {0};
        parser_add_error(&result, PARSE_INVALID_INPUT, 0, NULL, 0);
        return result;
    }
    if (p->flow == PARSER_NEED_TOKEN) {
        parser_add_error(&p->result, PARSE_INVALID_INPUT, p->next_index, NULL, TOKEN_BIT(TOK_EOF));
        p->flow = PARSER_STOPPED;
    }
    if (p->result.status != PARSE_OK) parser_ast_discard(p);
    return p->result;
}
