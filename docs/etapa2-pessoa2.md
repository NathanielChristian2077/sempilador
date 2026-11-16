# Etapa 2: parser, AST e análise semântica

## Parser e AST

O parser LL(1) utiliza tabelas geradas de `src/parser/grammar/first_follow.csv`
e do contrato de tokens. `make regen-parser` atualiza os arquivos gerados;
`make test-parser-generated` verifica sua consistência e o gerador.

A API incremental recebe tokens por `parser_stream_feed`. Comentários são
ignorados na análise. Erros invalidam a construção da AST.

A AST possui cópias dos textos, listas e dimensões. Os nós utilizam `NodeId`
estáveis; ponteiros retornados podem mudar após novas inserções. A publicação
verifica categorias, propriedade única e alcançabilidade. A liberação ocorre
por `ast_destroy`.

## Modelo semântico

`semantic_analyze` recebe uma AST publicada e produz um `SemanticModel`. O
modelo possui a tabela de símbolos e as informações por nó; empresta a AST.
Deve ser liberado por `semantic_model_destroy` antes da AST.

| Regra | Comportamento implementado |
| --- | --- |
| Funções | Assinaturas registradas antes dos corpos; permite recursão e chamadas adiantadas; sem sobrecarga |
| Escopos | Parâmetros e corpo externo compartilham escopo; blocos internos permitem sombreamento |
| Declarações | Duplicidade no mesmo escopo é erro; usos recebem `SymbolId` |
| Aritmética | Operandos `int` ou `float`; operações mistas convertem o operando `int` |
| Comparações | Operandos numéricos; resultado `boolean` |
| Lógica e condições | Exigem `boolean`; igualdade entre booleanos não é admitida |
| Atribuições, argumentos e retornos | Exigem tipos iguais, incluindo dimensões; não há conversão implícita `int` → `float` nesses contextos |
| Arrays | Dimensões positivas, com tamanho representável; índices `int`; índices constantes têm limites verificados |
| Constantes | Inteiros de 32 bits; `float` em binary64; divisor constante zero e divisão inteira excedida são erros |
| Retornos | Funções não `void` exigem retorno em todos os caminhos reconhecidos; laços não garantem retorno |
| Entrada e saída | `scanf` aceita destinos escalares; `println` aceita escalares e texto |

Essas regras descrevem a implementação atual. A gramática não permite
atribuição direta a elementos de array, chamadas como fatores de expressão
ou `return;` sem expressão.

## Diagnósticos e recursos

O modelo registra código, nó e posição de cada diagnóstico. São permitidos
até 32 diagnósticos e profundidade semântica de 256. Falhas de memória e limites
de recursos invalidam o modelo. Somente `semantic_model_valid()` autoriza a
geração LLVM.

`make test-core` inclui contratos, operadores, escopos, dimensões, AST, modelo,
recuperação do parser e falhas de alocação. Os alvos específicos são
`test-ast`, `test-ast-resource`, `test-semantic-analysis` e
`test-semantic-resources`.
