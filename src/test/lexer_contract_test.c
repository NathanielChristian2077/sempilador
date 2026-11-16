#define _DEFAULT_SOURCE 1
#include "../lexer/lexer.h"
#include "../parser/parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static int letter(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int digit(unsigned char c) { return c >= '0' && c <= '9'; }
static int space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/* Deliberately independent oracle: indexed, bounded scanning, no re2c rules. */
static TokenKind reference(const unsigned char *bytes, size_t n, size_t *offset,
                           size_t *start)
{
    size_t i = *offset;
    while (i < n && space(bytes[i])) ++i;
    *start = i;
    if (i == n) { *offset = i; return TOK_EOF; }
    TokenKind kind = TOK_INVALID;
    unsigned char c = bytes[i++];
    if (letter(c)) {
        while (i < n && (letter(bytes[i]) || digit(bytes[i]))) ++i;
        static const struct { const char *word; TokenKind kind; } keywords[] = {
            {"int", TOK_INT}, {"float", TOK_FLOAT}, {"char", TOK_CHAR},
            {"boolean", TOK_BOOLEAN}, {"void", TOK_VOID}, {"if", TOK_IF},
            {"else", TOK_ELSE}, {"for", TOK_FOR}, {"while", TOK_WHILE},
            {"scanf", TOK_SCANF}, {"println", TOK_PRINTLN}, {"main", TOK_MAIN},
            {"return", TOK_RETURN}, {"func", TOK_FUNC}
        };
        kind = TOK_ID;
        for (size_t k = 0; k < COUNT(keywords); ++k)
            if (i - *start == strlen(keywords[k].word) &&
                memcmp(bytes + *start, keywords[k].word, i - *start) == 0)
                kind = keywords[k].kind;
    } else if (digit(c) || c == '.') {
        while (i < n && (letter(bytes[i]) || digit(bytes[i]) || bytes[i] == '.')) ++i;
        size_t p = *start;
        while (p < i && digit(bytes[p])) ++p;
        if (p > *start) {
            if (p == i) kind = TOK_NUM_INT;
            else if (bytes[p++] == '.') {
                size_t decimals = p;
                while (p < i && digit(bytes[p])) ++p;
                if (p == i && p > decimals) kind = TOK_NUM_DEC;
            }
        }
    } else if (c == '"') {
        while (i < n && bytes[i] != '"' && bytes[i] != '\r' &&
               bytes[i] != '\n' && bytes[i] != 0) ++i;
        if (i < n && bytes[i] == '"') { ++i; kind = TOK_TEXTO; }
    } else if (c == '/' && i < n && bytes[i] == '/') {
        ++i;
        while (i < n && bytes[i] != '\r' && bytes[i] != '\n' && bytes[i] != 0) ++i;
        if (i < n && bytes[i] == '\r') ++i;
        if (i < n && bytes[i] == '\n') ++i;
        kind = TOK_COMMENT;
    } else {
        static const struct { const char *text; TokenKind kind; } operators[] = {
            {"==", TOK_COMP}, {"!=", TOK_COMP}, {"<=", TOK_COMP}, {">=", TOK_COMP},
            {"&&", TOK_AND}, {"||", TOK_OR}, {"=", TOK_ASSIGN}, {"!", TOK_NOT},
            {"<", TOK_COMP}, {">", TOK_COMP}, {"+", TOK_PLUS}, {"-", TOK_MINUS},
            {"*", TOK_STAR}, {"/", TOK_SLASH}, {"%", TOK_PERCENT},
            {"(", TOK_LPAREN}, {")", TOK_RPAREN}, {"[", TOK_LBRACKET},
            {"]", TOK_RBRACKET}, {"{", TOK_LBRACE}, {"}", TOK_RBRACE},
            {",", TOK_COMMA}, {";", TOK_SEMICOLON}
        };
        for (size_t k = 0; k < COUNT(operators); ++k) {
            size_t length = strlen(operators[k].text);
            if (length <= n - *start &&
                memcmp(bytes + *start, operators[k].text, length) == 0) {
                i = *start + length;
                kind = operators[k].kind;
                break;
            }
        }
    }
    *offset = i;
    return kind;
}

static void compare(const unsigned char *bytes, size_t length)
{
    Lexer lexer;
    lexer_init(&lexer, (const char *)bytes, length);
    size_t offset = 0, start;
    for (size_t step = 0; ; ++step) {
        assert(step <= length);
        TokenKind expected = reference(bytes, length, &offset, &start);
        Token actual = lexer_next(&lexer);
        if (actual.kind != expected || actual.length != offset - start) {
            fprintf(stderr, "lexer mismatch: length=%zu start=%zu expected=%d/%zu actual=%d/%zu\n",
                    length, start, expected, offset - start, actual.kind, actual.length);
            abort();
        }
        assert(actual.begin == lexer.source + start);
        assert(lexer.current == lexer.source + offset);
        uint32_t line = 1, column = 1;
        for (size_t i = 0; i < start; ++i) {
            if (bytes[i] == '\r') { ++line; column = 1; }
            else if (bytes[i] == '\n') {
                if (i == 0 || bytes[i - 1] != '\r') ++line;
                column = 1;
            } else ++column;
        }
        assert(actual.line == line && actual.column == column);
        if (expected == TOK_EOF) {
            assert(offset == length);
            for (size_t i = 0; i < 3; ++i) {
                Token again = lexer_next(&lexer);
                assert(again.kind == TOK_EOF && again.length == 0);
                assert(again.begin == actual.begin && again.line == line && again.column == column);
            }
            return;
        }
        assert(actual.length > 0);
    }
}

static void test_regressions(void)
{
    const char *invalid[] = {"2D", "123abc", "12.5x", "2e3", "12.", ".5",
        "1.2.3", "0x10", "2_", "00a", "9..", "9e+3"};
    for (size_t i = 0; i < COUNT(invalid); ++i) {
        compare((const unsigned char *)invalid[i], strlen(invalid[i]));
        Lexer lexer;
        lexer_init(&lexer, invalid[i], strlen(invalid[i]));
        Token t = lexer_next(&lexer);
        assert(t.kind == TOK_INVALID);
        if (i != COUNT(invalid) - 1) assert(t.length == strlen(invalid[i]));
    }
    const char *cases[] = {
        "", " ", "\r\n\r\n\n\r", "if ifx If _int int_1",
        "int float char boolean void if else for while scanf println main return func",
        "= == === ! != !== > >= < <= && & || |",
        "0 001 12 12.5 0.0 00.01 +2 -3 2+3 2 - 3 2 D",
        "//", "// no newline", "//\r\nx", "//\rx", "\"\" \"abc\"",
        "\"unterminated", "\"unterminated\r\nx", "\"x\"\"y\"", "\"\\\"",
        "int[2][3] m; // dimension\nmain(){println(m[1][2]);}",
        "int f(int a){return a;} main(){int x; x=func f(2); if(x>=1){println(\"ok\");}}"
    };
    for (size_t i = 0; i < COUNT(cases); ++i) {
        size_t n = strlen(cases[i]);
        /* Every prefix uses exactly n allocated bytes, without a terminator. */
        for (size_t end = 0; end <= n; ++end) {
            unsigned char *copy = end == 0 ? NULL : malloc(end);
            assert(end == 0 || copy != NULL);
            if (end) memcpy(copy, cases[i], end);
            compare(copy, end);
            free(copy);
        }
    }
    unsigned char nul[] = {'x', 0, 'y', ' ', '/', '/', 0, 'z', '"', 0, '"'};
    compare(nul, sizeof(nul));

    Token tokens[32];
    Lexer lexer;
    const char source[] = "main(){int[2D] a;}";
    lexer_init(&lexer, source, sizeof(source) - 1);
    size_t count = 0;
    do { assert(count < COUNT(tokens)); tokens[count++] = lexer_next(&lexer); }
    while (tokens[count - 1].kind != TOK_EOF);
    ParseResult parsed = parser_parse(tokens, count);
    assert(parsed.status == PARSE_LEXICAL_ERROR);
    assert(parsed.diagnostic_count == 1);
    assert(parsed.diagnostics[0].line == 1 && parsed.diagnostics[0].column == 12);
    assert(tokens[parsed.diagnostics[0].token_index].length == 2);
}

static uint32_t random_state = UINT32_C(0x93a025c7);
static uint32_t random_u32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void test_generated(void)
{
    unsigned char bytes[256];
    for (unsigned a = 0; a < 256; ++a) {
        bytes[0] = (unsigned char)a;
        compare(bytes, 1);
        for (unsigned b = 0; b < 256; ++b) {
            bytes[1] = (unsigned char)b;
            compare(bytes, 2);
        }
    }
    const char alphabet[] = "abcdefXYZ_int02.9\r\n\t\"/+-*%=!&|<>[]{}(),; ";
    for (size_t round = 0; round < 20000; ++round) {
        size_t length = random_u32() % sizeof(bytes);
        for (size_t i = 0; i < length; ++i)
            bytes[i] = round % 2 == 0 ? (unsigned char)random_u32()
                : (unsigned char)alphabet[random_u32() % (sizeof(alphabet) - 1)];
        compare(bytes, length);
    }
}

static void test_guard_page(void)
{
#if defined(__unix__)
    long page_size = sysconf(_SC_PAGESIZE);
    assert(page_size > 0);
    size_t page = (size_t)page_size;
    unsigned char *mapping = mmap(NULL, page * 2, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(mapping != MAP_FAILED);
    assert(mprotect(mapping + page, page, PROT_NONE) == 0);
    const char *inputs[] = {"", "x", "2D", "12.5", "\"incomplete", "//comment", "<", "&&", "\r\n"};
    for (size_t k = 0; k < COUNT(inputs); ++k)
        for (size_t n = 0; n <= strlen(inputs[k]); ++n) {
            unsigned char *start = mapping + page - n;
            memcpy(start, inputs[k], n);
            compare(start, n);
        }
    assert(munmap(mapping, page * 2) == 0);
#endif
}

int main(void)
{
    test_regressions();
    test_generated();
    test_guard_page();
    puts("lexer_contract_test: OK (regressions, every byte pair, 20000 seeded inputs, guard page)");
    return 0;
}
