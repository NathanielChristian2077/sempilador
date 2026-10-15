#include "../source/source.h"

#include <stdio.h>

int main(void)
{
    SourceBuffer source;

    SourceResult result = source_load("test/test.csim", &source);

    if (result != SOURCE_OK) {
        fprintf(stderr, "source_load falhou: código %d\n", result);
        return 1;
    }

    printf(
        "size: %zu bytes\n",
        source.length
    );

    fwrite(
        source.data,
        1,
        source.length,
        stdout
    );

    source_free(&source);

    return 0;
}
