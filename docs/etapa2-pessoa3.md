# Etapa 2: geração de LLVM IR

Este incremento traduz a AST validada pela análise semântica para LLVM IR em
texto e liga o fluxo completo na CLI: léxico, sintático, AST, semântica e
geração. O IR é verificado por `opt` e executado por `lli` nos testes.

## Compilar, gerar e executar

```sh
make -C src build
src/bin/sempilador programa.csim            # grava programa.ll ao lado
src/bin/sempilador programa.csim saida.ll   # escolhe o arquivo de saída
make -C src run-llvm FILE=programa.csim     # gera e executa com lli
make -C src test-codegen
```

- `lli` e `opt` vêm do LLVM; use `LLI=` e `OPT=` para outro caminho.
- Para um executável nativo: `clang programa.ll -o programa`.
- A CLI retorna 1 se houver erro léxico, sintático ou semântico; nesse caso
  nenhum `.ll` é gravado.

## Interface: `codegen/codegen.h`

```c
bool codegen_emit(const Ast *ast, const SemanticModel *model, FILE *out);
```

Só gera código para um modelo com `semantic_model_valid()`. Lê a AST e o
modelo sem alterá-los. Retorna `false` para modelo inválido, falta de memória
ou erro de escrita; nesse caso `out` pode conter texto parcial.

O gerador usa o que a semântica anota em cada nó: o tipo de cada expressão, o
`SymbolId` de nomes e declarações, as conversões int→float das operações
binárias e as constantes já calculadas.

## Decisões

| Assunto | Decisão |
| --- | --- |
| Formato | IR em texto com `fprintf`; não depende da biblioteca do LLVM |
| `int` / `float` / `char` / `boolean` | `i32` / `double` / `i8` / `i1` |
| Arrays | `int[2][3]` vira `[2 x [3 x i32]]`, acessado por `getelementptr` |
| Variáveis | Um `alloca` por símbolo no bloco de entrada, com `load`/`store`; dispensa `phi` |
| Nomes | Variável e parâmetro: `%v<SymbolId>`; função: `@f.<nome>`, que nunca colide com `printf`, `scanf` ou `main` |
| Valor inicial | Cada declaração é zerada ao ser executada, inclusive a cada volta de um laço |
| Constantes | Emitidas direto; `double` em hexadecimal, a forma exata do LLVM |
| `&&` e `\|\|` | Curto-circuito, como em C, usando uma variável temporária em vez de `phi` |
| Arrays por valor | Atribuição copia o conteúdo (`llvm.memmove`); parâmetro de array chega por ponteiro e é copiado na entrada da função |
| Retorno de array | Parâmetro oculto `ptr %result`; a função retorna `void` |
| `main` | `define i32 @main()`; sem `return`, retorna 0 |
| `println` | Um `printf` por comando, formato montado na compilação, mais `\n` no fim |
| Formatos de saída | `int` `%d`, `float` `%g`, `char` `%c`, texto `%s`, `boolean` como `true`/`false` |
| `scanf` | Um `scanf` por item: `int` `%d`, `float` `%lf`, `char` `" %c"` (pula espaços) |
| `scanf` de `boolean` | Lê um inteiro; diferente de zero é verdadeiro |
| Texto | Constante global; bytes fora do ASCII imprimível são escapados (`\C3`) |
| Verificações em execução | Nenhuma: índice fora do limite e divisão por zero com valores lidos se comportam como em C. A semântica já rejeita os casos constantes |

Código depois de um `return` vai para um bloco sem predecessores, para o IR
continuar válido. Uma função não `void` que chega ao fim termina com
`unreachable`, porque a semântica garante que todo caminho retorna.

## Testes

`test/codegen_test.c` confere o contrato de `codegen_emit` e detalhes do IR:
programa rejeitado não gera nada, constantes, conversões, `alloca` no bloco de
entrada, nomes de função, retorno de array, escape de texto e curto-circuito.

Cada `test/codegen/NOME.csim` é compilado pela CLI, verificado por
`opt -passes=verify`, executado por `lli` com `NOME.in` como entrada (se
existir) e comparado com `NOME.out`:

| Programa | Cobre |
| --- | --- |
| `aritmetica` | precedência, divisão e resto com negativos, `INT_MIN`, constantes |
| `real` | `float`, mistura com `int`, resto de `float`, formato `%g` |
| `logica` | comparações, `!`, `&&`/`\|\|` com curto-circuito protegendo divisão por zero, função `boolean` |
| `controle` | `while`, `for` completo, parcial e infinito, `if/else` aninhado, código após `return` |
| `funcoes` | recursão, parâmetros, função `void`, chamada como comando e em atribuição |
| `arrays` | matriz, sub-array, cópia por valor, array como parâmetro e como retorno |
| `escopo` | valor inicial zero, variável escondida em bloco interno, declaração em laço |
| `entrada` | `scanf` de todos os tipos, inclusive `boolean` e elementos de array |
| `texto` | `%` e `\` no texto, acentos, vários itens no mesmo `println` |

A gramática não permite atribuir a um elemento (`v[0] = 1;`); nos testes, os
elementos recebem valor pelo `scanf` ou por cópia do array inteiro.

## Compatibilidade com Windows

`semantic/semantic_constants.c` usava `newlocale`/`strtod_l`, que não existem no
MinGW. Um bloco `#ifdef _WIN32` mapeia esses nomes para `_create_locale`,
`_strtod_l` e `_free_locale`; no Linux nada muda.
