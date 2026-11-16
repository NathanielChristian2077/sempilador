#include "source/source.h"
#include "token/token.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "symbol/symbol.h"
#include "codegen/codegen.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const token_spelling[] = {
    [TOK_EOF]       = "fim de arquivo",
    [TOK_INVALID]   = "token invalido",
    [TOK_ID]        = "identificador",
    [TOK_NUM_INT]   = "numero inteiro",
    [TOK_NUM_DEC]   = "numero decimal",
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
    [TOK_COMMENT]   = "comentario"
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
        fprintf(stream, "entrada invalida para o parser.\n");
        return;
    }
    if (error->status == PARSE_LEXICAL_ERROR) {
        fprintf(stream, "erro lexico");
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
        fprintf(stream, "Analise sintatica: programa valido.\n");
        return;
    }
    fprintf(stream, "Analise sintatica: %zu erro(s).\n", result->diagnostic_count);
    for (size_t i = 0; i < result->diagnostic_count; ++i)
        print_diagnostic(&result->diagnostics[i], tokens, count, stream);
    if (result->limit_reached)
        fprintf(stream, "Limite de %d diagnosticos atingido.\n", PARSE_MAX_DIAGNOSTICS);
}

static void print_semantic(const SemanticResult *result, FILE *stream)
{
    if (result->status == SEMANTIC_OK) {
        fprintf(stream, "Analise semantica: programa valido.\n");
        return;
    }
    fprintf(stream, "Analise semantica: %zu erro(s).\n", result->diagnostic_count);
    for (size_t i = 0; i < result->diagnostic_count; ++i) {
        const SemanticDiagnostic *error = &result->diagnostics[i];
        if (error->span.line > 0)
            fprintf(stream, "Linha %" PRIu32 ", coluna %" PRIu32 ": ", error->span.line,
                    error->span.column);
        fprintf(stream, "%s.\n", semantic_diagnostic_message(error->code));
    }
    if (result->limit_reached)
        fprintf(stream, "Limite de %d diagnosticos atingido.\n", SEMANTIC_MAX_DIAGNOSTICS);
}

/* The .ll goes next to the input: replace its extension, or append one. */
static char *default_output(const char *input)
{
    size_t stem = strlen(input);
    for (size_t i = stem; i > 0; --i) {
        if (input[i - 1] == '/' || input[i - 1] == '\\')
            break;
        if (input[i - 1] == '.') {
            stem = i - 1;
            break;
        }
    }
    char *path = malloc(stem + sizeof(".ll"));
    if (path != NULL) {
        memcpy(path, input, stem);
        memcpy(path + stem, ".ll", sizeof(".ll"));
    }
    return path;
}

/* Build the AST, check it and write LLVM IR when the program is valid. */
static bool generate(const Token *tokens, size_t count, const char *output)
{
    Ast *ast = NULL;
    ParseResult parsed = parser_parse_ast(tokens, count, 0, &ast);
    if (parsed.status != PARSE_OK || ast == NULL) {
        fprintf(stderr, "Nao foi possivel construir a AST.\n");
        ast_destroy(ast);
        return false;
    }
    SemanticModel *model = NULL;
    SemanticResult result = semantic_analyze(ast, NULL, &model);
    print_semantic(&result, stdout);
    bool valid = semantic_model_valid(model);
    if (valid) {
        FILE *out = fopen(output, "wb");
        valid = out != NULL && codegen_emit(ast, model, out);
        if (out != NULL && fclose(out) != 0)
            valid = false;
        if (valid)
            printf("Codigo LLVM gerado em %s\n", output);
        else
            fprintf(stderr, "Nao foi possivel gravar '%s'.\n", output);
    }
    semantic_model_destroy(model);
    ast_destroy(ast);
    return valid;
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "uso: %s <arquivo> [saida.ll]\n", argv[0]);
        return EXIT_FAILURE;
    }

    SourceBuffer source;
    SourceResult loaded = source_load(argv[1], &source);
    if (loaded != SOURCE_OK) {
        fprintf(stderr, "Nao foi possivel ler '%s' (codigo %d).\n", argv[1], loaded);
        return EXIT_FAILURE;
    }

    size_t count = 0;
    Token *tokens = lex_all(&source, &count);
    SymbolTable table;
    if (tokens == NULL || !symbol_table_init(&table)) {
        fprintf(stderr, "Memoria insuficiente.\n");
        free(tokens);
        source_free(&source);
        return EXIT_FAILURE;
    }

    int status = EXIT_FAILURE;
    if (!symbol_table_add_tokens(&table, tokens, count)) {
        fprintf(stderr, "Memoria insuficiente.\n");
    } else {
        print_tokens(tokens, count, stdout);
        putchar('\n');
        symbol_table_print(&table, stdout);
        putchar('\n');
        ParseResult result = parser_parse(tokens, count);
        print_result(&result, tokens, count, stdout);
        if (result.status == PARSE_OK) {
            char *output = argc == 3 ? argv[2] : default_output(argv[1]);
            if (output == NULL)
                fprintf(stderr, "Memoria insuficiente.\n");
            else if (generate(tokens, count, output))
                status = EXIT_SUCCESS;
            if (output != argv[2])
                free(output);
        }
    }

    symbol_table_free(&table);
    free(tokens);
    source_free(&source);
    return status;
}
