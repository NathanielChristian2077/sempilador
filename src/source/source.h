#ifndef SOURCE_H
#define SOURCE_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t length;
} SourceBuffer;

typedef enum {
    SOURCE_OK = 0,
    SOURCE_ERR_OPEN,
    SOURCE_ERR_SEEK,
    SOURCE_ERR_TELL,
    SOURCE_ERR_ALLOC,
    SOURCE_ERR_READ
} SourceResult;

SourceResult source_load(const char *path, SourceBuffer *out);

void source_free(SourceBuffer *source);

#endif
