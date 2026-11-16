# sempilador

Compilador de C-Simplificado em C11. Implementa análise léxica e sintática,
construção da AST, análise semântica e geração de LLVM IR.

## Requisitos

| Operação | Dependências |
| --- | --- |
| Compilar o sempilador | GCC ou Clang com C11; GNU Make |
| Executar testes sem ferramentas LLVM | Python 3.9 ou superior |
| Verificar e executar o IR nos testes | LLVM com `opt` e `lli`, versão 15 ou superior |
| Regenerar ou verificar o lexer | Python; re2c 4.1 |

Os arquivos gerados estão versionados. O build normal utiliza esses arquivos
e não executa re2c ou o gerador do parser.

## Compilação e uso

Na raiz do repositório:

```sh
make
src/bin/sempilador src/test/valid.csim src/bin/valid.ll
```

A CLI aceita `sempilador <entrada> [saida.ll]`. Sem saída explícita, grava o
IR ao lado da entrada. Erros léxicos, sintáticos ou semânticos retornam 1 e
impedem nova geração de IR.

Para executar o IR: `lli src/bin/valid.ll`. Para gerar um executável nativo:
`clang src/bin/valid.ll -o programa`.

### Windows

Utilizar GCC/MinGW e GNU Make no `PATH`. No PowerShell ou CMD:

```powershell
mingw32-make SHELL=cmd.exe
.\src\bin\sempilador.exe .\src\test\valid.csim .\src\bin\valid.ll
mingw32-make SHELL=cmd.exe PYTHON=python test-core
```

Se o GNU Make estiver instalado como `make`, substituir `mingw32-make` por
`make`. As receitas nativas não exigem Git Bash ou utilitários Unix.

## Verificação

| Comando na raiz | Escopo |
| --- | --- |
| `make test-core` | Testes C, CLI e geração das tabelas LL(1); dispensa `opt` e `lli` |
| `make test` | Suíte completa, incluindo verificação e execução do LLVM IR |
| `make test-build-system` | Build limpo sem re2c, timestamps e diagnóstico de lexer ausente |
| `make test-sanitize` | Suíte completa com ASan e UBSan; requer suporte do compilador |
| `make check-generated` | Compara o lexer versionado com a saída do re2c |
| `make clean` | Remove o diretório de compilação |

`make -C src <alvo>` permanece disponível. Em `make run FILE=...` e
`make run-llvm FILE=...`, o caminho de entrada é relativo ao diretório onde
o Make foi chamado. `BUILD_DIR` é relativo a `src`.

Variáveis configuráveis: `CC`, `CFLAGS`, `CPPFLAGS`, `LDFLAGS`, `LDLIBS`,
`BUILD_DIR`, `PYTHON`, `RE2C`, `OPT` e `LLI`. Ao alterar compilador ou flags,
executar `make clean` ou utilizar outro `BUILD_DIR`.

## Arquivos gerados

Após alterar `src/lexer/lexer.re`, executar `make regen-lexer`. Após alterar
a gramática ou o contrato de tokens, executar `make regen-parser`. Versionar
os arquivos regenerados junto das respectivas fontes.

O CI executa a suíte completa com GCC/Clang e sanitizers no Linux. No Windows,
executa `test-core` e `test-build-system` com GCC/MinGW e CMD.

## Contratos

- [Léxico, tipos e símbolos](docs/etapa2-pessoa1.md)
- [Parser, AST e análise semântica](docs/etapa2-pessoa2.md)
- [Geração LLVM](docs/etapa2-pessoa3.md)
