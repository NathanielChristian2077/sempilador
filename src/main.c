#include "source/source.h"
#include "token/token.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "symbol/symbol.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static const char *const token_spelling[] = {
    [TOK_EOF]       = "fim de arquivo",
    [TOK_INVALID]   = "token inválido",
    [TOK_ID]        = "identificador",
    [TOK_NUM_INT]   = "número inteiro",
    [TOK_NUM_DEC]   = "número decimal",
    [TOK_TEXTO]     = "texto",
    [TOK_INT]       = "'int'",
    [TOK_FLOAT]     = "'float'",
    [TOK_CHAR]      = "'char'",
    [TOK_BOOLEAN]   = "'boolean'",
    [TOK_VOID]      = "'void'",
    [TOK_IF]        = "'if'",
    [TOK_ELSE]      = "'else'",
    [TOK_FOR]       = "'for'",
    [TOK_WHILE]     = "'while'",
    [TOK_SCANF]     = "'scanf'",
    [TOK_PRINTLN]   = "'println'",
    [TOK_MAIN]      = "'main'",
    [TOK_RETURN]    = "'return'",
    [TOK_FUNC]      = "'func'",
    [TOK_ASSIGN]    = "'='",
    [TOK_PLUS]      = "'+'",
    [TOK_MINUS]     = "'-'",
    [TOK_STAR]      = "'*'",
    [TOK_SLASH]     = "'/'",
    [TOK_PERCENT]   = "'%'",
    [TOK_AND]       = "'&&'",
    [TOK_OR]        = "'||'",
    [TOK_NOT]       = "'!'",
    [TOK_COMP]      = "operador relacional",
    [TOK_LPAREN]    = "'('",
    [TOK_RPAREN]    = "')'",
    [TOK_LBRACKET]  = "'['",
    [TOK_RBRACKET]  = "']'",
    [TOK_LBRACE]    = "'{'",
    [TOK_RBRACE]    = "'}'",
    [TOK_COMMA]     = "','",
    [TOK_SEMICOLON] = "';'",
    [TOK_COMMENT]   = "comentário"
};

/* Collect tokens up to EOF, keeping invalid ones for the parser. */
static Token *lex_all(const SourceBuffer *source, size_t *count)
{
    size_t capacity = 256;
    Token *tokens = malloc(capacity * sizeof(Token));
    if (tokens == NULL)
        return NULL;
    Lexer lexer;
    lexer_init(&lexer, source->data, source->length);
    *count = 0;
    do {
        if (*count == capacity) {
            Token *grown = capacity > SIZE_MAX / 2 / sizeof(Token) ? NULL :
                           realloc(tokens, capacity * 2 * sizeof(Token));
            if (grown == NULL) {
                free(tokens);
                return NULL;
            }
            tokens = grown;
            capacity *= 2;
        }
        tokens[(*count)++] = lexer_next(&lexer);
    } while (tokens[*count - 1].kind != TOK_EOF);
    return tokens;
}

static void print_tokens(const Token *tokens, size_t count, FILE *stream)
{
    fprintf(stream, "Tokens (%zu)\n", count);
    for (size_t i = 0; i < count; ++i) {
        fprintf(stream, "%5" PRIu32 ":%-4" PRIu32 " %-14s ",
                tokens[i].line, tokens[i].column, token_kind_name(tokens[i].kind));
        fwrite(tokens[i].begin, 1, tokens[i].length, stream);
        fputc('\n', stream);
    }
}

static void print_diagnostic(const ParseDiagnostic *error, const Token *tokens,
                             size_t count, FILE *stream)
{
    if (error->has_location)
        fprintf(stream, "Linha %" PRIu32 ", coluna %" PRIu32 ": ", error->line, error->column);
    if (error->status == PARSE_INVALID_INPUT) {
        fprintf(stream, "entrada inválida para o parser.\n");
        return;
    }
    if (error->status == PARSE_LEXICAL_ERROR) {
        fprintf(stream, "erro léxico");
    } else {
        fprintf(stream, "esperado ");
        bool first = true;
        for (size_t kind = TOK_EOF; kind <= TOK_COMMENT; ++kind) {
            if (!error->expected[kind])
                continue;
            fprintf(stream, "%s%s", first ? "" : ", ", token_spelling[kind]);
            first = false;
        }
    }
    /* found is only a safe index after validating the category. */
    if (error->has_found && error->token_index < count &&
        (unsigned)error->found <= (unsigned)TOK_COMMENT) {
        const Token *token = &tokens[error->token_index];
        fprintf(stream, ", encontrado %s", token_spelling[error->found]);
        if (token->length > 0 && error->found != TOK_EOF) {
            fprintf(stream, " \"");
            fwrite(token->begin, 1, token->length, stream);
            fputc('"', stream);
        }
    }
    fprintf(stream, ".\n");
}

static void print_result(const ParseResult *result, const Token *tokens,
                         size_t count, FILE *stream)
{
    if (result->status == PARSE_OK) {
        fprintf(stream, "Análise sintática: programa válido.\n");
        return;
    }
    fprintf(stream, "Análise sintática: %zu erro(s).\n", result->diagnostic_count);
    for (size_t i = 0; i < result->diagnostic_count; ++i)
        print_diagnostic(&result->diagnostics[i], tokens, count, stream);
    if (result->limit_reached)
        fprintf(stream, "Limite de %d diagnósticos atingido.\n", PARSE_MAX_DIAGNOSTICS);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "uso: %s <arquivo>\n", argv[0]);
        return EXIT_FAILURE;
    }

    SourceBuffer source;
    SourceResult loaded = source_load(argv[1], &source);
    if (loaded != SOURCE_OK) {
        fprintf(stderr, "Não foi possível ler '%s' (código %d).\n", argv[1], loaded);
        return EXIT_FAILURE;
    }

    size_t count = 0;
    Token *tokens = lex_all(&source, &count);
    SymbolTable table;
    if (tokens == NULL || !symbol_table_init(&table)) {
        fprintf(stderr, "Memória insuficiente.\n");
        free(tokens);
        source_free(&source);
        return EXIT_FAILURE;
    }

    int status = EXIT_FAILURE;
    if (!symbol_table_add_tokens(&table, tokens, count)) {
        fprintf(stderr, "Memória insuficiente.\n");
    } else {
        print_tokens(tokens, count, stdout);
        putchar('\n');
        symbol_table_print(&table, stdout);
        putchar('\n');
        ParseResult result = parser_parse(tokens, count);
        print_result(&result, tokens, count, stdout);
        if (result.status == PARSE_OK)
            status = EXIT_SUCCESS;
    }

    symbol_table_free(&table);
    free(tokens);
    source_free(&source);
    return status;
}
