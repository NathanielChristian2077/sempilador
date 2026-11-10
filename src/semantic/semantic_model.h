#ifndef SEMPILADOR_SEMANTIC_MODEL_H
#define SEMPILADOR_SEMANTIC_MODEL_H

#include "../parser/ast.h"
#include "../symbol/semantic.h"

#define SEMANTIC_MAX_DIAGNOSTICS 32
#define SEMANTIC_MAX_DEPTH 256

typedef enum { SEM_UNANALYZED, SEM_NOT_APPLICABLE, SEM_VALID, SEM_ERROR } SemanticState;
typedef enum {
    SEMANTIC_OK,
    SEMANTIC_ERRORS,
    SEMANTIC_INVALID_INPUT,
    SEMANTIC_NO_MEMORY,
    SEMANTIC_LIMIT_EXCEEDED
} SemanticStatus;
typedef struct {
    bool is_string;
    Type concrete;
} SemanticType;
typedef enum { SEM_CONVERT_NONE, SEM_CONVERT_INT_FLOAT } SemanticConversion;
typedef enum {
    SEM_CONSTANT_NONE,
    SEM_CONSTANT_INT,
    SEM_CONSTANT_FLOAT,
    SEM_CONSTANT_BOOLEAN,
    SEM_CONSTANT_TEXT
} SemanticConstantKind;
typedef struct {
    SemanticConstantKind kind;
    union {
        int64_t integer;
        double decimal;
        bool boolean;
        AstText text;
    } as;
} SemanticConstant;
typedef struct {
    SemanticState state, type_state;
    SemanticType type;
    SymbolId symbol;
    ScopeId scope;
    /* Binary operand slots; conversions are contextual, not changes of child type. */
    SemanticConversion left_conversion, right_conversion;
    SemanticConstant constant;
} SemanticInfo;
typedef enum {
    SEM_DIAG_DIMENSION,
    SEM_DIAG_DUPLICATE,
    SEM_DIAG_UNDECLARED,
    SEM_DIAG_NOT_VARIABLE,
    SEM_DIAG_NOT_FUNCTION,
    SEM_DIAG_OPERATOR,
    SEM_DIAG_ASSIGNMENT,
    SEM_DIAG_CONDITION,
    SEM_DIAG_ARGUMENT_COUNT,
    SEM_DIAG_ARGUMENT_TYPE,
    SEM_DIAG_RETURN_TYPE,
    SEM_DIAG_MISSING_RETURN,
    SEM_DIAG_INDEX_TYPE,
    SEM_DIAG_INDEX_RANK,
    SEM_DIAG_INDEX_BOUNDS,
    SEM_DIAG_INPUT_TYPE,
    SEM_DIAG_OUTPUT_TYPE,
    SEM_DIAG_LITERAL_RANGE,
    SEM_DIAG_ZERO_DIVISOR,
    SEM_DIAG_DIVISION_OVERFLOW,
    SEM_DIAG_RESOURCE_MEMORY,
    SEM_DIAG_RESOURCE_LIMIT,
    SEM_DIAG_INVALID_AST
} SemanticDiagnosticCode;
typedef struct {
    SemanticDiagnosticCode code;
    NodeId node;
    SourceSpan span;
    /* Declaration or offending argument when useful; otherwise AST_NODE_NONE. */
    NodeId related;
} SemanticDiagnostic;
typedef struct {
    SemanticStatus status;
    size_t diagnostic_count;
    SemanticDiagnostic diagnostics[SEMANTIC_MAX_DIAGNOSTICS];
    bool limit_reached, complete;
} SemanticResult;
typedef struct SemanticModel SemanticModel;

/* Borrows a published AST for the lifetime of the model. Owns its SemanticTable,
 * node results and any converted dimension storage. The allocator context must
 * outlive the model. *out is NULL if creation fails, otherwise even a partial
 * model can be inspected and must be destroyed. No emitter/CLI is called.
 */
SemanticResult semantic_analyze(const Ast *ast, const ParserAllocator *allocator,
                                SemanticModel **out);
const SemanticResult *semantic_model_result(const SemanticModel *model);
const SemanticInfo *semantic_model_info(const SemanticModel *model, NodeId node);
const SemanticTable *semantic_model_symbols(const SemanticModel *model);
bool semantic_model_valid(const SemanticModel *model);
const char *semantic_diagnostic_message(SemanticDiagnosticCode code);
void semantic_model_destroy(SemanticModel *model);

/* Integer constants normally fit int32. The sole exception is magnitude
 * 2147483648 in the literal directly under unary minus, representing INT_MIN.
 * A NONE constant means unknown, never an assumed zero. Text views exclude
 * quotes and borrow the AST. Arrays retain value semantics; no LLVM ABI here.
 */
#endif
