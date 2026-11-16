#include "parser_internal.h"

void parser_track(ParserStream *p, TokenKind kind)
{
    switch (kind) {
    case TOK_LPAREN: ++p->depth.round; break;
    case TOK_RPAREN: if (p->depth.round) --p->depth.round; break;
    case TOK_LBRACKET: ++p->depth.square; break;
    case TOK_RBRACKET: if (p->depth.square) --p->depth.square; break;
    case TOK_LBRACE:
        p->depth.round = p->depth.square = 0;
        ++p->depth.braces;
        break;
    case TOK_RBRACE: if (p->depth.braces) --p->depth.braces; break;
    default: break;
    }
}

bool parser_begin_recovery(ParserStream *p, const Token *token, LL1Symbol failed)
{
    bool commands = failed == LL1_NONTERMINAL(LL1_NT_COMANDOS) &&
                    token->kind != TOK_EOF && token->kind != TOK_MAIN;
    bool functions = failed == LL1_NONTERMINAL(LL1_NT_LISTAFUNCOES) ||
                     failed == LL1_NONTERMINAL(LL1_NT_PROGRAMA);
    if (!commands && !functions && p->context_count != 0) {
        p->recovery = p->contexts[--p->context_count];
        p->size = p->recovery.resume;
    } else {
        RecoveryKind kind;
        if (commands) kind = RECOVER_COMMAND;
        else if (functions && token->kind != TOK_EOF) kind = RECOVER_FUNCTION;
        else return false;
        p->recovery = (ParserContext){p->size, p->index, p->depth, kind, token->kind};
    }
    p->mode = RECOVERY_SKIP;
    p->else_seen = false;
    return true;
}

static bool candidate(TokenKind kind, RecoveryKind context)
{
    uint64_t types = ll1_first[LL1_NT_TIPOBASE];
    if (context == RECOVER_FUNCTION)
        return ((types | TOKEN_BIT(TOK_VOID) | TOKEN_BIT(TOK_MAIN)) & TOKEN_BIT(kind)) != 0;
    uint64_t commands = ll1_first[LL1_NT_COMANDO] | TOKEN_BIT(TOK_RBRACE);
    if (context == RECOVER_DECLARATION) commands |= types;
    return (commands & TOKEN_BIT(kind)) != 0;
}

bool parser_recover_token(ParserStream *p, Token token)
{
    if (p->mode == RECOVERY_ELSE) {
        p->mode = RECOVERY_NONE;
        if (token.kind != TOK_ELSE) return false;
        p->mode = RECOVERY_SKIP;
        p->else_seen = true;
        return true;
    }
    if (p->mode == RECOVERY_ID) {
        p->mode = RECOVERY_SKIP;
        if (token.kind == TOK_ASSIGN) {
            size_t index = p->index;
            p->mode = RECOVERY_NONE;
            p->index = p->pending_index;
            parser_process_token(p, p->pending_id);
            p->index = index;
            return false;
        }
    }
    ParserContext *r = &p->recovery;
    size_t braces = p->depth.braces > r->depth.braces ? p->depth.braces - r->depth.braces : 0;
    bool flat = braces == 0 && p->depth.round <= r->depth.round &&
                p->depth.square <= r->depth.square;
    if (token.kind == TOK_EOF || token.kind == TOK_MAIN ||
        (token.kind == TOK_RBRACE && braces == 0 && r->kind != RECOVER_FUNCTION)) {
        p->mode = RECOVERY_NONE;
        return false;
    }
    if (flat && p->index > r->start && candidate(token.kind, r->kind)) {
        if (token.kind == TOK_ID) {
            p->pending_id = token;
            p->pending_index = p->index;
            p->mode = RECOVERY_ID;
            return true;
        }
        p->mode = RECOVERY_NONE;
        return false;
    }
    bool control = r->first == TOK_FOR || r->first == TOK_WHILE || r->first == TOK_IF;
    if (token.kind == TOK_SEMICOLON && braces == 0 &&
        (r->kind == RECOVER_DECLARATION || (!control && r->kind != RECOVER_FUNCTION) || flat)) {
        p->mode = RECOVERY_NONE;
        parser_track(p, token.kind);
        return true;
    }
    bool closes = token.kind == TOK_RBRACE && braces == 1;
    parser_track(p, token.kind);
    if (closes)
        p->mode = r->first == TOK_IF && !p->else_seen ? RECOVERY_ELSE : RECOVERY_NONE;
    return true;
}
