# Etapa 2: contratos de léxico, tipos e símbolos

Este incremento entrega o lexer em re2c e uma infraestrutura semântica testável
antes da integração com a AST. Não implementa ainda o percurso semântico da
pessoa 2 nem a geração LLVM da pessoa 3. O executável continua executando a
análise léxica e sintática da etapa 1.

## Compilar e verificar

```sh
make -C src build
make -C src test
make -C src check-generated
make -C src test-sanitize
```

- C11, GCC ou Clang e GNU Make são necessários.
- `lexer/lexer.re` é a fonte canônica; `lexer/lexer.c` é gerado e versionado.
- re2c **4.1** gera a versão conferida no CI, sem data, versão ou diretivas de
  caminho. `make -C src regen-lexer` regenera o arquivo. Se alterar o `.re`, a
  compilação também exige re2c para regenerar o `.c`.
- A compilação pode usar o `.c` versionado sem re2c quando ele está atualizado.
- Use `RE2C=/caminho/re2c` para uma instalação local e `CC=clang` para trocar o
  compilador. Use um `BUILD_DIR` diferente ao trocar flags ou compilador: o Make
  não detecta alterações nas flags de uma compilação anterior.
- Os testes mantêm `assert` ativo mesmo com `CFLAGS=-DNDEBUG`.
- Os testes negativos da CLI exigem código de saída 1, não apenas qualquer
  falha. O alvo de sanitizers usa códigos 99/98 para não confundir um erro de
  memória ou comportamento indefinido com a rejeição esperada de um programa.
- `test-sanitize` roda toda a suíte em outro diretório, com ASan e UBSan. A
  detecção de vazamentos requer um ambiente que permita LeakSanitizer (o CI
  solicita `detect_leaks=1`). Em ambientes com ptrace, pode ser necessário usar
  `ASAN_OPTIONS=detect_leaks=0`; isso desativa somente a detecção de vazamentos.

## Contrato léxico

`lexer_init()` empresta exatamente `length` bytes. Não requer terminador NUL,
padding ou acesso a bytes depois da entrada. `Token.begin` continua apontando
para a entrada: mantenha o buffer vivo enquanto tokens ou AST usarem esses
lexemas. `NULL` representa uma entrada vazia.

As posições são linhas e colunas de bytes, começando em 1. CRLF conta como uma
quebra de linha. Contadores saturam em `UINT32_MAX`. Comentários continuam
retornando `TOK_COMMENT`, e o parser os ignora. Chamadas repetidas no fim
retornam o mesmo EOF vazio. NUL dentro da entrada é inválido; não encerra o
arquivo silenciosamente.

| Entrada | Resultado |
| --- | --- |
| `2`, `001`, `12.5` | Inteiro, inteiro e decimal |
| `2D`, `123abc`, `12.5x`, `2e3`, `0x10` | Um `TOK_INVALID` com todo o trecho |
| `12.`, `.5`, `1.2.3` | Um `TOK_INVALID` com todo o trecho |
| `2 D` | Inteiro e identificador distintos |
| `2+3` | Inteiro, soma e inteiro |
| `9e+3` | `TOK_INVALID("9e")`, soma e inteiro; não é notação científica |
| `// comentário` sem quebra final | Comentário válido |
| Texto sem aspas finais | Inválido até a quebra de linha, NUL ou EOF |

Um trecho numérico malformado consome a sequência contígua de letras ASCII,
algarismos, `_` e `.` iniciada por algarismo ou ponto. Números válidos ganham
empates; um trecho inválido mais longo impede aceitar apenas um prefixo.

Decisões preservadas: sinal é um operador separado; não há notação científica,
hexadecimal, escapes de texto nem literais `true`/`false`. O tratamento de texto
preserva a regra anterior: a primeira aspa dupla encerra o literal; uma barra
invertida não protege uma aspa. Os tokens `for` e `func` são mantidos, pois o
parser atual aceita ambos. Isso não amplia a gramática existente neste incremento.

## Tipos: `type/type.h`

`Type` é uma visão emprestada, sem alocação própria. Escalares têm `rank=0` e
`dimensions=NULL`. `int[2][3]` usa `base=TYPE_INT`, `rank=2`, dimensões `{2,3}`.

- `type_valid`: rejeita tipos desconhecidos, dimensões nulas/zero, arrays de
  `void` e multiplicações de tamanho que excedam `SIZE_MAX`.
- `type_equal`: compara base, quantidade e ordem das dimensões; tipos inválidos
  nunca são iguais, nem a si mesmos.
- `type_size`: segue SDT2: char=1, boolean=1, int=4 e float=8 bytes. Isso é o
  modelo didático, não o ABI do host nem uma decisão de layout do LLVM.
- `type_indexed`: remove dimensões pela esquerda e retorna outra visão.
- `type_format`: formata diagnósticos com semântica de truncamento de `snprintf`;
  retorna o comprimento necessário ou `SIZE_MAX` para argumentos inválidos.

`void` só é válido como escalar e não tem tamanho de armazenamento. A categoria
léxica `TEXTO` não introduz um tipo declarativo `string` nesta API. A pessoa 2
deve decidir a tipagem de expressões e literais segundo as regras da linguagem.
Ao converter dimensões dos tokens para `size_t`, a pessoa 2 deve verificar todos
os dígitos e overflow, sem depender de conversões que aceitam prefixos.

## Símbolos: `symbol/semantic.h`

O módulo antigo `symbol/symbol.h` permanece como listagem de ocorrências para
compatibilidade com a CLI da etapa 1. Ele **não** é a tabela semântica. A nova
`SemanticTable` só recebe declarações explícitas pela API abaixo.

```c
SemanticTable table;
if (!semantic_table_init(&table)) { /* tratar falha de memória */ }

SymbolDeclaration d = {
    .name = "contador", .length = 8, .kind = SYMBOL_VARIABLE,
    .type = {TYPE_INT, 0, NULL}, .line = 2, .column = 5
};
SymbolId id;
SymbolResult result = semantic_declare(&table, &d, &id);
/* Tratar result antes de usar id. Guardar id na AST, não SemanticSymbol *. */

semantic_table_free(&table);
```

| Operação | Contrato |
| --- | --- |
| `semantic_table_init` | Cria o escopo global; retorna false se falhar |
| `semantic_enter_scope` | Cria um filho do escopo ativo |
| `semantic_leave_scope` | Volta ao pai; falha no global |
| `semantic_declare` | Copia os dados e registra uma declaração no escopo ativo |
| `semantic_lookup_current` | Procura apenas no escopo ativo |
| `semantic_resolve` | Procura no ativo e depois nos ancestrais |
| `semantic_symbol` | Recupera uma declaração por seu ID, inclusive após sair do escopo |

`SymbolId` e `ScopeId` são estáveis até liberar a tabela. As sentinelas são
`SYMBOL_ID_NONE` e `SCOPE_ID_NONE`; o escopo global é `SCOPE_GLOBAL`. Um ponteiro
retornado por `semantic_symbol` deve ser consultado novamente após declarar
símbolos, porque o vetor pode mudar de endereço.

Nomes, dimensões, retorno e tipos de parâmetros são copiados profundamente. Os
dados de entrada podem ser temporários. Na assinatura de função, `type` é o
retorno, e `parameters` guarda apenas os tipos, na ordem declarada. Declare os
nomes dos parâmetros no escopo da função com `SYMBOL_PARAMETER`.

Não há sobrecarga: qualquer declaração com mesmo nome no mesmo escopo retorna
`SYMBOL_DUPLICATE` e o ID anterior. Escopos distintos permitem nomes iguais.
Cabe ao percurso semântico impor restrições adicionais de sombreamento e decidir
se parâmetros e variáveis do corpo externo compartilham um escopo. Use um único
escopo se a regra for rejeitar a redeclaração de um parâmetro no corpo da função.

Uma falha de alocação não insere declaração parcial nem invalida IDs anteriores;
a capacidade interna pode crescer durante uma operação que depois falha. Falhas
distintas retornam `SYMBOL_INVALID` ou `SYMBOL_NO_MEMORY`. Inicialize uma tabela
nova e libere uma tabela viva antes de reinicializá-la. Tabelas não são thread-safe.

## Integração que falta

Pessoa 2: construir AST, cadastrar assinaturas na ordem/política permitida,
abrir/fechar escopos, declarar variáveis/parâmetros, vincular cada uso a um
`SymbolId` e verificar operações, atribuições, condições, chamadas e retornos.
O registro das assinaturas antes de visitar corpos permite recursão e chamadas
adiantadas, mas a política de linguagem precisa autorizar essas chamadas.

Pessoa 3: consumir os tipos e IDs resolvidos; manter seu próprio mapeamento de
`SymbolId` para valores/endereços LLVM. Não há endereço físico, deslocamento de
frame ou objeto LLVM nos símbolos deste incremento. A tabela deve continuar
viva durante essa geração.

## Evidência dos testes

- Regressão de `2D` no lexer, no diagnóstico do parser e na execução da CLI.
- Comparação com reconhecedor independente para cada um dos 65.536 pares de
  bytes, todos os bytes isolados e 20.000 entradas aleatórias com seed fixa.
- Cada prefixo das amostras é testado sem byte terminador disponível.
- Em Unix, uma página sem permissão de acesso logo após a entrada detecta
  leituras além do buffer, inclusive em tokens incompletos.
- Tipos: dimensões, indexação, comparação, formatação truncada e overflow.
- Símbolos: duplicidade, escopos ancestrais/irmãos, assinaturas ordenadas, cópia
  profunda, 6.000 declarações e 200 escopos aninhados.
- Alocador de teste falha em cada alocação de um cenário completo, verifica
  recuperação e conta alocações vivas após a liberação.
- Todos os testes anteriores continuam incluídos. CI executa GCC e Clang,
  warnings como erros, reprodução do código gerado e sanitizers.

Materiais: Aula4-Expressões regulares, Aula9-Gramática LL(1),
Aula13-SDT2-Declaração de Variáveis LL(1) e o
[manual do re2c](https://re2c.org/manual/manual_c.html).
