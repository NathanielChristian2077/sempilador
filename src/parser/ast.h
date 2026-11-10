#ifndef SEMPILADOR_AST_H
#define SEMPILADOR_AST_H

#include "parser.h"

/* End positions and last_token are exclusive; token indices include comments.
 * Columns follow the lexer's byte-based convention. source is caller-defined. */
typedef struct {
    uint32_t source;
    size_t first_token, last_token;
    uint32_t line, column, end_line, end_column;
} SourceSpan;
typedef size_t NodeId;
#define AST_NODE_NONE SIZE_MAX

typedef struct {
    const char *data;
    size_t length;
} AstText;
typedef struct {
    AstText text;
    SourceSpan span;
} AstName;
typedef struct {
    AstText lexeme;
    SourceSpan span;
} AstDimension;
typedef enum { AST_CHAR, AST_FLOAT, AST_INT, AST_BOOLEAN, AST_VOID } AstTypeBase;
typedef struct {
    AstTypeBase base;
    const AstDimension *dimensions;
    size_t rank;
    SourceSpan span;
} TypeSyntax;
typedef struct {
    const NodeId *items;
    size_t count;
} AstList;

typedef enum {
    AST_PROGRAM,
    AST_MAIN,
    AST_FUNCTION_DECL,
    AST_PARAM_DECL,
    AST_VAR_DECL,
    AST_BLOCK,
    AST_INT_LITERAL,
    AST_FLOAT_LITERAL,
    AST_TEXT_LITERAL,
    AST_NAME,
    AST_INDEX,
    AST_UNARY,
    AST_BINARY,
    AST_CALL,
    AST_ASSIGN,
    AST_CALL_STMT,
    AST_READ_STMT,
    AST_WRITE_STMT,
    AST_RETURN_STMT,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_FOR_STMT,
    AST_KIND_COUNT
} AstKind;
typedef enum { AST_POSITIVE, AST_NEGATIVE, AST_NOT } AstUnaryOperator;
typedef enum {
    AST_ADD,
    AST_SUBTRACT,
    AST_MULTIPLY,
    AST_DIVIDE,
    AST_REMAINDER,
    AST_AND,
    AST_OR,
    AST_LT,
    AST_LE,
    AST_GT,
    AST_GE,
    AST_EQ,
    AST_NE
} AstBinaryOperator;

typedef struct {
    NodeId id;
    AstKind kind;
    SourceSpan span;
    union {
        struct {
            AstList functions;
            NodeId main;
        } program;
        struct {
            NodeId body;
        } main;
        struct {
            AstName name;
            TypeSyntax return_type;
            AstList parameters;
            NodeId body;
        } function;
        struct {
            AstName name;
            TypeSyntax type;
        } declaration;
        struct {
            AstList declarations, statements;
        } block;
        struct {
            AstText lexeme;
            SourceSpan literal_span;
        } literal;
        AstName name;
        struct {
            NodeId base;
            AstList indices;
        } index;
        struct {
            AstUnaryOperator op;
            NodeId operand;
            SourceSpan operator_span;
        } unary;
        struct {
            AstBinaryOperator op;
            NodeId left, right;
            SourceSpan operator_span;
        } binary;
        struct {
            AstName callee;
            AstList arguments;
        } call;
        struct {
            NodeId target, value;
        } assign;
        struct {
            NodeId call;
        } call_stmt;
        struct {
            AstList items;
        } io;
        struct {
            NodeId value;
        } return_stmt;
        struct {
            NodeId condition, then_block, else_block;
        } if_stmt;
        struct {
            NodeId condition, body;
        } while_stmt;
        struct {
            NodeId initializer, condition, update, body;
        } for_stmt;
    } as;
} AstNode;

typedef struct Ast Ast;
/* Builder API for parser and manually constructed backend fixtures. All strings,
 * lists and dimensions supplied to append are copied. Children must already
 * exist. Failed operations leave status sticky; destroy always remains valid.
 * Views returned by node may move after append. IDs and owned texts stay stable.
 */
ParseStatus ast_create(Ast **out, const ParserAllocator *allocator);
NodeId ast_append(Ast *ast, const AstNode *node);
const AstNode *ast_node(const Ast *ast, NodeId id);
size_t ast_count(const Ast *ast);
ParseStatus ast_status(const Ast *ast);
/* Checks category constraints, single-parent ownership and reachability, then
 * freezes the tree. Does not perform language type checking. */
ParseStatus ast_publish(Ast *ast, NodeId program);
NodeId ast_root(const Ast *ast);
void ast_destroy(Ast *ast);

/* AST generation is opt-in. Recognition-only APIs keep their existing behavior.
 * take_ast transfers ownership once, only after EOF with PARSE_OK; otherwise NULL.
 * The AST allocator context must outlive the transferred tree. */
ParseStatus parser_stream_create_ast(ParserStream **out, const ParserConfig *config,
                                     uint32_t source);
Ast *parser_stream_take_ast(ParserStream *stream);
ParseResult parser_parse_ast(const Token *tokens, size_t count, uint32_t source, Ast **out);

#endif
