
#ifndef TOKEN_H
#define TOKEN_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TOK_EOF,
    TOK_INVALID,

    TOK_ID,
    TOK_NUM_INT,
    TOK_NUM_DEC,
    TOK_TEXTO,

    TOK_INT,
    TOK_FLOAT,
    TOK_CHAR,
    TOK_BOOLEAN,
    TOK_VOID,
    TOK_IF,
    TOK_ELSE,
    TOK_FOR,
    TOK_WHILE,
    TOK_SCANF,
    TOK_PRINTLN,
    TOK_MAIN,
    TOK_RETURN,
    TOK_FUNC,

    TOK_ASSIGN,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_COMP,

    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_SEMICOLON,

    TOK_COMMENT
} TokenKind;

typedef struct {
    TokenKind kind;
    const char *begin;
    size_t length;
    uint32_t line;
    uint32_t column;
} Token;

const char *token_kind_name(TokenKind kind);

#endif
