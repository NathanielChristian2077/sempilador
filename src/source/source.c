#include "source.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

SourceResult source_load(const char *path, SourceBuffer *out) {
    if (path == NULL ||out == NULL)
        return SOURCE_ERR_OPEN;

    out -> data = NULL;
    out -> length = 0;

    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return SOURCE_ERR_OPEN;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return SOURCE_ERR_SEEK;
    }

    long file_size = ftell(file);
    if(file_size < 0) {
        fclose(file);
        return SOURCE_ERR_TELL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return SOURCE_ERR_SEEK;
    }

    size_t length = (size_t)file_size;
    /* +1 for the terminator; ftell fits in long, so it cannot wrap size_t. */
    char *data = (char*)malloc(length + 1);
    if (data == NULL) {
        fclose(file);
        return SOURCE_ERR_ALLOC;
    }

    size_t bytes_read = fread(data, 1, length, file);
    fclose(file);
    if (bytes_read != length) {
        free(data);
        return SOURCE_ERR_READ;
    }

    data[length] = '\0';
    out->data = data;
    out->length = length;
    return SOURCE_OK;
}

void source_free(SourceBuffer *source) {
    if (source == NULL)
        return;

    free(source->data);

    source->data = NULL;
    source->length = 0;
}
