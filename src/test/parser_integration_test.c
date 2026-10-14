#include "../parser/parser.h"
#include "../lexer/lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *source;
    bool valid;
} Case;

#define VALID(name, source) {name, source, true}
#define INVALID(name, source) {name, source, false}
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static void check(const char *name, const char *source, bool valid)
{
    Token tokens[4096];
    size_t count = 0;
    Lexer lexer;
    lexer_init(&lexer, source, strlen(source));
    do {
        if (count == COUNT(tokens)) {
            fprintf(stderr, "Vetor de teste cheio: %s\n", name);
            exit(EXIT_FAILURE);
        }
        tokens[count++] = lexer_next(&lexer);
    } while (tokens[count - 1].kind != TOK_EOF);
    ParseResult result = parser_parse(tokens, count);
    if ((result.status == PARSE_OK) != valid) {
        fprintf(stderr, "Falha: %s (esperado %s)\n", name, valid ? "válido" : "inválido");
        exit(EXIT_FAILURE);
    }
    if (result.status != PARSE_OK) {
        const ParseDiagnostic *error = &result.diagnostic;
        if (!error->has_found || !error->has_location || error->token_index >= count) {
            fprintf(stderr, "Diagnóstico sem posição: %s\n", name);
            exit(EXIT_FAILURE);
        }
        if (result.status == PARSE_SYNTAX_ERROR) {
            bool has_expected = false;
            for (size_t i = 0; i <= TOK_COMMENT; ++i)
                has_expected = has_expected || error->expected[i];
            if (!has_expected) {
                fprintf(stderr, "Diagnóstico sem esperado: %s\n", name);
                exit(EXIT_FAILURE);
            }
        }
    }
    
    if (valid) {
        size_t close = count - 1;
        while (close > 0 && tokens[close].kind != TOK_RBRACE)
            --close;
        for (size_t n = 0; n <= close; ++n) {
            Token saved = tokens[n];
            tokens[n] = (Token){ .kind = TOK_EOF };
            bool accepted = parser_parse(tokens, n + 1).status == PARSE_OK;
            tokens[n] = saved;
            if (accepted) {
                fprintf(stderr, "Prefixo inválido aceito: %s, token %zu\n", name, n);
                exit(EXIT_FAILURE);
            }
        }
    }
}

int main(void)
{
    const Case cases[] = {
        VALID("mínimo", "main() {}"),
        VALID("comentários", "// início\nmain// comentário\n(){// bloco\n}//fim"),
        VALID("tipos e listas", "main(){char c; float f; int x,y,z; boolean b;}"),
        VALID("dimensões", "main(){int[10][20] a,b; float[2] c;}"),
        VALID("funções e parâmetros", "void f(){} int[3] g(char x,float[2] y,boolean z){return 1;} main(){}"),
        VALID("aritmética", "main(){x=1+2*3-4/2%2; y=+x; z=-2.5;}"),
        VALID("lógica", "main(){x=!!a || b && c < 3; y=(a+b)*(c-1);}"),
        VALID("texto como fator", "main(){x=\"abc\"; y=!(1<2);}"),
        VALID("índices", "main(){x=a[i+1][2*3]; y=a[(i<3)];}"),
        VALID("scanf", "main(){scanf(x,a[i+1][2]);}"),
        VALID("println", "main(){println(x,a[1],2,3.5,\"ok\");}"),
        VALID("chamadas", "main(){func f() func g(1,a+2,\"x\") x=func h();}"),
        VALID("retorno", "main(){return x+1;}"),
        VALID("seleção", "main(){if(x){int y; y=2;} else {println(1);} if(y){} }"),
        VALID("while", "main(){while(x<3){x=x+1;}}"),
        VALID("aninhamento", "main(){while(x){if(y){for(;;){if(z){}else{}}}else{}}}"),
        VALID("for completo", "main(){int i; for(i=0;i<10;i=i+1){println(i);}}"),
        INVALID("vazio", ""),
        INVALID("apenas comentário", "// fim"),
        INVALID("main tipado", "int main(){}"),
        INVALID("dois principais", "main(){} main(){}"),
        INVALID("função após main", "main(){} void f(){}"),
        INVALID("sem principal", "void f(){}"),
        INVALID("parâmetro em main", "main(int x){}"),
        INVALID("declaração inicializada", "main(){int x=1;}"),
        INVALID("declaração após comando", "main(){x=1; int y;}"),
        INVALID("tipo void em variável", "main(){void x;}"),
        INVALID("dimensão após id", "main(){int x[2];}"),
        INVALID("dimensão decimal", "main(){int[2.5] x;}"),
        INVALID("dimensão vazia", "main(){int[] x;}"),
        INVALID("lista id incompleta", "main(){int x,;}"),
        INVALID("destino indexado", "main(){x[0]=1;}"),
        INVALID("atribuição vazia", "main(){x=;}"),
        INVALID("operando ausente", "main(){x=1+;}"),
        INVALID("comparação encadeada", "main(){x=a<b<c;}"),
        INVALID("sinal antes de parênteses", "main(){x=-(a+1);}"),
        INVALID("sinal antes de texto", "main(){x=-\"a\";}"),
        INVALID("sinal duplicado", "main(){x=--a;}"),
        INVALID("índice relacional direto", "main(){x=a[i<3];}"),
        INVALID("chamada como fator", "main(){x=1+func f();}"),
        INVALID("chamada dentro de argumento", "main(){func f(func g())}"),
        INVALID("chamada comum de C", "main(){f();}"),
        INVALID("chamada isolada com ;", "main(){func f();}"),
        INVALID("argumento final vazio", "main(){func f(1,)}"),
        INVALID("parâmetro final vazio", "int f(int x,){} main(){}"),
        INVALID("void em parâmetros", "void f(void){} main(){}"),
        INVALID("scanf vazio", "main(){scanf();}"),
        INVALID("scanf constante", "main(){scanf(1);}"),
        INVALID("println vazio", "main(){println();}"),
        INVALID("println expressão", "main(){println(x+1);}"),
        INVALID("println sinal", "main(){println(-1);}"),
        INVALID("return vazio", "main(){return;}"),
        INVALID("if sem bloco", "main(){if(x)x=1;}"),
        INVALID("else if", "main(){if(x){}else if(y){}}"),
        INVALID("else órfão", "main(){else{}}"),
        INVALID("while vazio", "main(){while(){}}"),
        INVALID("bloco isolado", "main(){{}}"),
        INVALID("comando vazio", "main(){;}"),
        INVALID("for declaração", "main(){for(int i=0;i<2;i=i+1){}}"),
        INVALID("for incremento", "main(){for(i=0;i<2;i++){}}"),
        INVALID("for chamada", "main(){for(i=func f();;){}}"),
        INVALID("for sem separador", "main(){for(i=0 i<2;i=i+1){}}"),
        INVALID("for atualização expressão", "main(){for(;;i+1){}}"),
        INVALID("léxico inválido", "main(){@}"),
        INVALID("texto incompleto", "main(){x=\"abc;}"),
        INVALID("fecha chave ausente", "main(){int x;"),
        INVALID("conteúdo após programa", "main(){} x"),
    };
    for (size_t i = 0; i < COUNT(cases); ++i)
        check(cases[i].name, cases[i].source, cases[i].valid);

    /* As oito combinações das três partes opcionais do for. */
    for (unsigned mask = 0; mask < 8; ++mask) {
        char source[128];
        snprintf(source, sizeof(source), "main(){for(%s;%s;%s){}}",
                 mask & 1 ? "i=0" : "", mask & 2 ? "i<3" : "",
                 mask & 4 ? "i=i+1" : "");
        check("partes opcionais do for", source, true);
    }
    printf("parser_integration_test: OK (%zu casos + 8 for + prefixos truncados)\n", COUNT(cases));
    return 0;
}
