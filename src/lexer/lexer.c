
#include "lexer.h"

#include <string.h>

typedef enum {
    CC_OTHER,
    CC_IDENT,
    CC_DIGIT,
    CC_SPACE,
    CC_QUOTE,
    CC_OPERATOR,
    CC_DELIMITER
} CharClass;

static const uint8_t char_class[256] = {
    ['a'] = CC_IDENT, ['b'] = CC_IDENT, ['c'] = CC_IDENT,
    ['d'] = CC_IDENT, ['e'] = CC_IDENT, ['f'] = CC_IDENT,
    ['g'] = CC_IDENT, ['h'] = CC_IDENT, ['i'] = CC_IDENT,
    ['j'] = CC_IDENT, ['k'] = CC_IDENT, ['l'] = CC_IDENT,
    ['m'] = CC_IDENT, ['n'] = CC_IDENT, ['o'] = CC_IDENT,
    ['p'] = CC_IDENT, ['q'] = CC_IDENT, ['r'] = CC_IDENT,
    ['s'] = CC_IDENT, ['t'] = CC_IDENT, ['u'] = CC_IDENT,
    ['v'] = CC_IDENT, ['w'] = CC_IDENT, ['x'] = CC_IDENT,
    ['y'] = CC_IDENT, ['z'] = CC_IDENT,

    ['A'] = CC_IDENT, ['B'] = CC_IDENT, ['C'] = CC_IDENT,
    ['D'] = CC_IDENT, ['E'] = CC_IDENT, ['F'] = CC_IDENT,
    ['G'] = CC_IDENT, ['H'] = CC_IDENT, ['I'] = CC_IDENT,
    ['J'] = CC_IDENT, ['K'] = CC_IDENT, ['L'] = CC_IDENT,
    ['M'] = CC_IDENT, ['N'] = CC_IDENT, ['O'] = CC_IDENT,
    ['P'] = CC_IDENT, ['Q'] = CC_IDENT, ['R'] = CC_IDENT,
    ['S'] = CC_IDENT, ['T'] = CC_IDENT, ['U'] = CC_IDENT,
    ['V'] = CC_IDENT, ['W'] = CC_IDENT, ['X'] = CC_IDENT,
    ['Y'] = CC_IDENT, ['Z'] = CC_IDENT,

    ['_'] = CC_IDENT,

    ['0'] = CC_DIGIT, ['1'] = CC_DIGIT,
    ['2'] = CC_DIGIT, ['3'] = CC_DIGIT,
    ['4'] = CC_DIGIT, ['5'] = CC_DIGIT,
    ['6'] = CC_DIGIT, ['7'] = CC_DIGIT,
    ['8'] = CC_DIGIT, ['9'] = CC_DIGIT,

    [' ']  = CC_SPACE,
    ['\t'] = CC_SPACE,
    ['\n'] = CC_SPACE,
    ['\r'] = CC_SPACE,
    ['\v'] = CC_SPACE,
    ['\f'] = CC_SPACE,

    ['"'] = CC_QUOTE,

    ['='] = CC_OPERATOR,
    ['+'] = CC_OPERATOR,
    ['-'] = CC_OPERATOR,
    ['*'] = CC_OPERATOR,
    ['/'] = CC_OPERATOR,
    ['%'] = CC_OPERATOR,
    ['&'] = CC_OPERATOR,
    ['|'] = CC_OPERATOR,
    ['!'] = CC_OPERATOR,
    ['<'] = CC_OPERATOR,
    ['>'] = CC_OPERATOR,

    ['('] = CC_DELIMITER,
    [')'] = CC_DELIMITER,
    ['['] = CC_DELIMITER,
    [']'] = CC_DELIMITER,
    ['{'] = CC_DELIMITER,
    ['}'] = CC_DELIMITER,
    [','] = CC_DELIMITER,
    [';'] = CC_DELIMITER
};

static int at_end(const Lexer *lexer) {
    return lexer->current == lexer->end;
}

static int peek(const Lexer *lexer) {
    return at_end(lexer) ? -1 : (unsigned char)*lexer->current;
}

static int peek_next(const Lexer *lexer) {
    return lexer->end - lexer->current < 2 ? -1 : (unsigned char)lexer->current[1];
}

static unsigned char advance(Lexer *lexer) {
    const char *pos = lexer->current;
    unsigned char c = (unsigned char)*pos;

    lexer->current = pos + 1;

    if(c == '\r') {
        ++lexer->line;
        lexer->column = 1;
    } else if (c == '\n') {
        if (pos == lexer->source || pos[-1] != '\r')
            ++lexer->line;
        lexer->column = 1;
    } else {
        ++lexer->column;
    }

    return c;
}

static Token make_token(
    const Lexer *lexer,
    TokenKind kind,
    const char *begin,
    uint32_t line,
    uint32_t column
) {
    return (Token){
        .kind = kind,
        .begin = begin,
        .length = (size_t)(lexer->current - begin),
        .line = line,
        .column = column
    };
}


static TokenKind keyword_kind(
    const char *s,
    size_t length
) {
    switch (length) {
    case 2:
        if (memcmp(s, "if", 2) == 0)
            return TOK_IF;
        break;

    case 3:
        if (memcmp(s, "int", 3) == 0)
            return TOK_INT;
        if (memcmp(s, "for", 3) == 0)
            return TOK_FOR;
        break;

    case 4:
        if (memcmp(s, "char", 4) == 0)
            return TOK_CHAR;
        if (memcmp(s, "else", 4) == 0)
            return TOK_ELSE;
        if (memcmp(s, "void", 4) == 0)
            return TOK_VOID;
        if (memcmp(s, "main", 4) == 0)
            return TOK_MAIN;
        if (memcmp(s, "func", 4) == 0)
            return TOK_FUNC;
        break;

    case 5:
        if (memcmp(s, "float", 5) == 0)
            return TOK_FLOAT;
        if (memcmp(s, "while", 5) == 0)
            return TOK_WHILE;
        if (memcmp(s, "scanf", 5) == 0)
            return TOK_SCANF;
        break;

    case 6:
        if (memcmp(s, "return", 6) == 0)
            return TOK_RETURN;
        break;

    case 7:
        if (memcmp(s, "boolean", 7) == 0)
            return TOK_BOOLEAN;
        if (memcmp(s, "println", 7) == 0)
            return TOK_PRINTLN;
        break;
    }

    return TOK_ID;
}

static Token scan_identifier(
    Lexer *lexer,
    const char *begin,
    uint32_t line,
    uint32_t column
) {
    do {
        advance(lexer);
    } while (
        !at_end(lexer) &&
        (
            char_class[(unsigned char)*lexer->current] == CC_IDENT ||
            char_class[(unsigned char)*lexer->current] == CC_DIGIT
        )
    );

    TokenKind kind = keyword_kind(
        begin,
        (size_t)(lexer->current - begin)
    );

    return make_token(
        lexer, kind, begin, line, column
    );
}


static Token scan_number(
    Lexer *lexer,
    const char *begin,
    uint32_t line,
    uint32_t column
) {
    do {
        advance(lexer);
    } while (
        !at_end(lexer) &&
        char_class[(unsigned char)*lexer->current] == CC_DIGIT
    );

    TokenKind kind = TOK_NUM_INT;

    if (
        peek(lexer) == '.' &&
        peek_next(lexer) >= '0' &&
        peek_next(lexer) <= '9'
    ) {
        advance(lexer);

        do {
            advance(lexer);
        } while (
            !at_end(lexer) &&
            char_class[(unsigned char)*lexer->current] == CC_DIGIT
        );

        kind = TOK_NUM_DEC;
    }

    return make_token(
        lexer, kind, begin, line, column
    );
}

static Token scan_string(
    Lexer *lexer,
    const char *begin,
    uint32_t line,
    uint32_t column
) {
    advance(lexer); // Aspas iniciais

    while (!at_end(lexer)) {
        int c = peek(lexer);

        if (c == '"') {
            advance(lexer);
            return make_token(
                lexer, TOK_TEXTO, begin, line, column
            );
        }

        if (c == '\r' || c == '\n')
            break;

        advance(lexer);
    }

    return make_token(
        lexer, TOK_INVALID, begin, line, column
    );
}


static Token scan_operator(
    Lexer *lexer,
    const char *begin,
    uint32_t line,
    uint32_t column
) {
    unsigned char c = advance(lexer);
    TokenKind kind = TOK_INVALID;

    switch (c) {
    case '+':
        kind = TOK_PLUS;
        break;
    case '-':
        kind = TOK_MINUS;
        break;
    case '*':
        kind = TOK_STAR;
        break;
    case '%':
        kind = TOK_PERCENT;
        break;

    case '/':
        if (peek(lexer) == '/') {
            advance(lexer);

            while (
                !at_end(lexer) &&
                peek(lexer) != '\n' &&
                peek(lexer) != '\r'
            ) {
                advance(lexer);
            }

            if (peek(lexer) == '\r')
                advance(lexer);
            if (peek(lexer) == '\n')
                advance(lexer);

            kind = TOK_COMMENT;
        } else {
            kind = TOK_SLASH;
        }
        break;

    case '=':
        kind = TOK_ASSIGN;
        if (peek(lexer) == '=') {
            advance(lexer);
            kind = TOK_COMP;
        }
        break;

    case '!':
        kind = TOK_NOT;
        if (peek(lexer) == '=') {
            advance(lexer);
            kind = TOK_COMP;
        }
        break;

    case '<':
    case '>':
        kind = TOK_COMP;
        if (peek(lexer) == '=')
            advance(lexer);
        break;

    case '&':
        if (peek(lexer) == '&') {
            advance(lexer);
            kind = TOK_AND;
        }
        break;

    case '|':
        if (peek(lexer) == '|') {
            advance(lexer);
            kind = TOK_OR;
        }
        break;
    }

    return make_token(
        lexer, kind, begin, line, column
    );
}

static Token scan_delimiter(
    Lexer *lexer,
    const char *begin,
    uint32_t line,
    uint32_t column
) {
    unsigned char c = advance(lexer);
    TokenKind kind = TOK_INVALID;

    switch (c) {
    case '(': kind = TOK_LPAREN; break;
    case ')': kind = TOK_RPAREN; break;
    case '[': kind = TOK_LBRACKET; break;
    case ']': kind = TOK_RBRACKET; break;
    case '{': kind = TOK_LBRACE; break;
    case '}': kind = TOK_RBRACE; break;
    case ',': kind = TOK_COMMA; break;
    case ';': kind = TOK_SEMICOLON; break;
    }

    return make_token(
        lexer, kind, begin, line, column
    );
}


void lexer_init(
    Lexer *lexer,
    const char *source,
    size_t length
) {
    lexer->source = source;
    lexer->current = source;
    lexer->end = source + length;
    lexer->line = 1;
    lexer->column = 1;
}

Token lexer_next(Lexer *lexer)
{
    for (;;) {
        const char *begin = lexer->current;
        uint32_t line = lexer->line;
        uint32_t column = lexer->column;

        if (at_end(lexer)) {
            return make_token(
                lexer, TOK_EOF, begin, line, column
            );
        }

        switch (char_class[(unsigned char)*lexer->current]) {
        case CC_SPACE:
            advance(lexer);
            break;

        case CC_IDENT:
            return scan_identifier(
                lexer, begin, line, column
            );

        case CC_DIGIT:
            return scan_number(
                lexer, begin, line, column
            );

        case CC_QUOTE:
            return scan_string(
                lexer, begin, line, column
            );

        case CC_OPERATOR:
            return scan_operator(
                lexer, begin, line, column
            );

        case CC_DELIMITER:
            return scan_delimiter(
                lexer, begin, line, column
            );

        default:
            advance(lexer);
            return make_token(
                lexer, TOK_INVALID, begin, line, column
            );
        }
    }
}
