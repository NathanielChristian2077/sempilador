CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2

.PHONY: test test-source test-token test-lexer

test: test-source test-token test-lexer

test-source:
	@$(CC) $(CFLAGS) -Isource \
		test/source_test.c source/source.c \
		-o /tmp/source_test
	@/tmp/source_test

test-token:
	@$(CC) $(CFLAGS) -Itoken \
		test/token_test.c token/token.c \
		-o /tmp/token_test
	@/tmp/token_test

test-lexer:
	@$(CC) $(CFLAGS) -Itoken -Ilexer \
		test/lexer_test.c lexer/lexer.c \
		-o /tmp/lexer_test
	@/tmp/lexer_test

BENCH_CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic

.PHONY: test-lexer-regression bench-gen bench-build \
        bench-run bench-freeze

test: test-lexer-regression

test-lexer-regression:
	@$(CC) -std=c11 -g -O1 \
		-Wall -Wextra -Wpedantic \
		-fsanitize=address,undefined \
		-fno-omit-frame-pointer \
		-Itoken -Ilexer \
		test/lexer_regression_test.c lexer/lexer.c \
		-o /tmp/lexer_regression_test
	@/tmp/lexer_regression_test

bench-gen:
	@python3 bench/generate_corpus.py

bench-build:
	@$(CC) $(BENCH_CFLAGS) \
		-Isource -Itoken -Ilexer \
		bench/lexer_bench.c \
		lexer/lexer.c source/source.c \
		-o /tmp/lexer_bench

bench-run: bench-gen bench-build
	@mkdir -p bench/results
	@printf 'file,bytes,tokens,repeats,median_ms,p95_ms,mib_per_s,ns_per_token,checksum\n' \
		> bench/results/current.csv
	@for file in bench/corpus/*.txt; do \
		/tmp/lexer_bench "$$file" \
			>> bench/results/current.csv || exit 1; \
	done
	@{ \
		date -Is; \
		git rev-parse HEAD; \
		$(CC) --version | head -n 1; \
		uname -m; \
		lscpu | grep -m 1 'Model name'; \
		sha256sum bench/corpus/*.txt; \
	} > bench/results/current.meta.txt
	@echo "Resultados: bench/results/current.csv"

bench-freeze: bench-run
	@test ! -e bench/results/baseline-v0.csv || { \
		echo "Baseline ja registrada. Nao sobrescrever."; \
		exit 1; \
	}
	@cp bench/results/current.csv \
		bench/results/baseline-v0.csv
	@cp bench/results/current.meta.txt \
		bench/results/baseline-v0.meta.txt
	@echo "Baseline V0 registrada."
