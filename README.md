# sempilador

Compilador didático de C-Simplificado, em C11.

```sh
make -C src build
make -C src test
make -C src run FILE=test/valid.csim
```

A CLI realiza análise léxica e sintática e mostra os identificadores encontrados.
A infraestrutura da etapa 2 inclui tipos e tabela semântica com escopos; a
integração com a AST e a geração LLVM ainda está pendente.

Os contratos, decisões da linguagem, instruções de re2c e testes estão em
[docs/etapa2-pessoa1.md](docs/etapa2-pessoa1.md).
