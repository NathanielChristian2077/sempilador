#include "parser_internal.h"

#include <stdlib.h>
#include <string.h>

static void *allocate(void *context, size_t bytes)
{
    (void)context;
    return malloc(bytes);
}

static void release(void *context, void *memory)
{
    (void)context;
    free(memory);
}

void parser_resource_error(ParserStream *p, ParseStatus status, const Token *token)
{
    parser_add_error(&p->result, status, p->index, token, 0);
    p->flow = PARSER_STOPPED;
}

static bool grow(ParserStream *p, void **storage, size_t *capacity, size_t needed,
                 size_t width, const void *local, const Token *token)
{
    if (needed <= *capacity) return true;
    size_t next = *capacity;
    while (next < needed) {
        if (next > p->max_stack / 2) { next = p->max_stack; break; }
        next *= 2;
    }
    if (next > SIZE_MAX / width) {
        parser_resource_error(p, PARSE_LIMIT_EXCEEDED, token);
        return false;
    }
    void *memory = p->allocator.allocate(p->allocator.context, next * width);
    if (memory == NULL) {
        parser_resource_error(p, PARSE_NO_MEMORY, token);
        return false;
    }
    memcpy(memory, *storage, *capacity * width);
    if (*storage != local) p->allocator.release(p->allocator.context, *storage);
    *storage = memory;
    *capacity = next;
    return true;
}

bool parser_reserve(ParserStream *p, size_t symbols, size_t contexts, const Token *token)
{
    if (symbols > p->max_stack || contexts > p->max_stack) {
        parser_resource_error(p, PARSE_LIMIT_EXCEEDED, token);
        return false;
    }
    void *stack = p->stack;
    if (!grow(p, &stack, &p->capacity, symbols, sizeof(*p->stack), p->inline_stack, token))
        return false;
    p->stack = stack;
    void *frames = p->contexts;
    if (!grow(p, &frames, &p->context_capacity, contexts, sizeof(*p->contexts),
              p->inline_contexts, token)) return false;
    p->contexts = frames;
    return true;
}

ParseStatus parser_stream_initialize(ParserStream *p, const ParserConfig *config,
                                     LL1Nonterminal start, bool require_eof)
{
    *p = (ParserStream){0};
    p->allocator = config != NULL ? config->allocator : (ParserAllocator){0};
    p->max_stack = config != NULL && config->max_stack != 0 ? config->max_stack : 65536;
    p->stack = p->inline_stack;
    p->capacity = PARSER_INLINE_SYMBOLS;
    p->contexts = p->inline_contexts;
    p->context_capacity = PARSER_INLINE_CONTEXTS;
    if ((p->allocator.allocate == NULL) != (p->allocator.release == NULL))
        return PARSE_INVALID_INPUT;
    if (p->max_stack > SIZE_MAX / sizeof(ParserContext)) return PARSE_INVALID_INPUT;
    if (p->allocator.allocate == NULL)
        p->allocator = (ParserAllocator){NULL, allocate, release};
    if (!parser_reserve(p, require_eof ? 2 : 1, 0, NULL)) return p->result.status;
    if (require_eof) p->stack[p->size++] = TOK_EOF;
    p->stack[p->size++] = LL1_NONTERMINAL(start);
    return PARSE_OK;
}

void parser_stream_dispose(ParserStream *p)
{
    if (p->stack != p->inline_stack) p->allocator.release(p->allocator.context, p->stack);
    if (p->contexts != p->inline_contexts) p->allocator.release(p->allocator.context, p->contexts);
}

ParseStatus parser_stream_create(ParserStream **out, const ParserConfig *config)
{
    if (out == NULL) return PARSE_INVALID_INPUT;
    *out = NULL;
    ParserStream local;
    ParseStatus status = parser_stream_initialize(&local, config, LL1_NT_PROGRAMA, true);
    if (status != PARSE_OK) return status;
    ParserStream *p = local.allocator.allocate(local.allocator.context, sizeof(*p));
    if (p == NULL) return PARSE_NO_MEMORY;
    /* Initialize at its final address: inline-storage pointers cannot be copied. */
    status = parser_stream_initialize(p, config, LL1_NT_PROGRAMA, true);
    if (status != PARSE_OK) {
        local.allocator.release(local.allocator.context, p);
        return status;
    }
    *out = p;
    return PARSE_OK;
}

void parser_stream_destroy(ParserStream *p)
{
    if (p == NULL) return;
    ParserAllocator allocator = p->allocator;
    parser_stream_dispose(p);
    allocator.release(allocator.context, p);
}
