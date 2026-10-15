#include "../token/token.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char source[] = "int contador = 42;";

    Token token = {
        .kind   = TOK_ID,
        .begin  = source + 4,
        .length = 8,
        .line   = 1,
        .column = 5
    };

    assert(token.kind == TOK_ID);
    assert(token.length == 8);
    assert(token.line == 1);
    assert(token.column == 5);

    assert(memcmp(token.begin,
                  "contador",
                  token.length) == 0);

    assert(strcmp(token_kind_name(token.kind),
                  "TOK_ID") == 0);

    printf("Kind: %s\n", token_kind_name(token.kind));

    printf("Lexeme: ");
    fwrite(token.begin, 1, token.length, stdout);
    putchar('\n');

    printf("Position: %u:%u\n",
           token.line, token.column);

    puts("Token test passed.");

    return 0;
}
