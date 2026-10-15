CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2
BUILD_DIR ?= bin

SOURCES = source/source.c token/token.c lexer/lexer.c parser/parser.c symbol/symbol.c

.PHONY: build run clean

build: $(BUILD_DIR)/sempilador

$(BUILD_DIR)/sempilador: main.c $(SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) main.c $(SOURCES) -o $@

run: build
	$(BUILD_DIR)/sempilador $(or $(FILE),test/valid.csim)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: test test-source test-token test-lexer

test: test-source test-token test-lexer

test-source: | $(BUILD_DIR)
	@$(CC) $(CFLAGS) -Isource \
		test/source_test.c source/source.c \
		-o $(BUILD_DIR)/source_test
	@$(BUILD_DIR)/source_test

test-token: | $(BUILD_DIR)
	@$(CC) $(CFLAGS) -Itoken \
		test/token_test.c token/token.c \
		-o $(BUILD_DIR)/token_test
	@$(BUILD_DIR)/token_test

test-lexer: | $(BUILD_DIR)
	@$(CC) $(CFLAGS) -Itoken -Ilexer \
		test/lexer_test.c lexer/lexer.c \
		-o $(BUILD_DIR)/lexer_test
	@$(BUILD_DIR)/lexer_test


.PHONY: test-parser test-parser-integration test-parser-diagnostics test-parser-recovery test-parser-sanitize

test: test-parser test-parser-integration test-parser-diagnostics test-parser-recovery

test-parser: | $(BUILD_DIR)
	$(CC) $(CFLAGS) test/parser_test.c parser/parser.c -o $(BUILD_DIR)/sempilador_parser_test
	$(BUILD_DIR)/sempilador_parser_test

test-parser-integration: | $(BUILD_DIR)
	$(CC) $(CFLAGS) test/parser_integration_test.c parser/parser.c lexer/lexer.c -o $(BUILD_DIR)/sempilador_parser_integration_test
	$(BUILD_DIR)/sempilador_parser_integration_test

test-parser-diagnostics: | $(BUILD_DIR)
	$(CC) $(CFLAGS) test/parser_diagnostic_test.c parser/parser.c lexer/lexer.c -o $(BUILD_DIR)/sempilador_parser_diagnostic_test
	$(BUILD_DIR)/sempilador_parser_diagnostic_test

test-parser-recovery: | $(BUILD_DIR)
	$(CC) $(CFLAGS) test/parser_recovery_test.c parser/parser.c lexer/lexer.c -o $(BUILD_DIR)/sempilador_parser_recovery_test
	$(BUILD_DIR)/sempilador_parser_recovery_test

PARSER_SAN_FLAGS = -std=c11 -g -O1 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer

test-parser-sanitize: | $(BUILD_DIR)
	$(CC) $(PARSER_SAN_FLAGS) test/parser_test.c parser/parser.c -o $(BUILD_DIR)/sempilador_parser_test_san
	$(BUILD_DIR)/sempilador_parser_test_san
	$(CC) $(PARSER_SAN_FLAGS) test/parser_integration_test.c parser/parser.c lexer/lexer.c -o $(BUILD_DIR)/sempilador_parser_integration_test_san
	$(BUILD_DIR)/sempilador_parser_integration_test_san
	$(CC) $(PARSER_SAN_FLAGS) test/parser_diagnostic_test.c parser/parser.c lexer/lexer.c -o $(BUILD_DIR)/sempilador_parser_diagnostic_test_san
	$(BUILD_DIR)/sempilador_parser_diagnostic_test_san
	$(CC) $(PARSER_SAN_FLAGS) test/parser_recovery_test.c parser/parser.c lexer/lexer.c -o $(BUILD_DIR)/sempilador_parser_recovery_test_san
	$(BUILD_DIR)/sempilador_parser_recovery_test_san

.PHONY: test-symbol test-symbol-sanitize bench-symbol

test: test-symbol test-build

test-symbol: | $(BUILD_DIR)
	$(CC) $(CFLAGS) test/symbol_test.c symbol/symbol.c lexer/lexer.c -o $(BUILD_DIR)/sempilador_symbol_test
	$(BUILD_DIR)/sempilador_symbol_test

test-symbol-sanitize: | $(BUILD_DIR)
	$(CC) $(PARSER_SAN_FLAGS) test/symbol_test.c symbol/symbol.c lexer/lexer.c -o $(BUILD_DIR)/sempilador_symbol_test_san
	$(BUILD_DIR)/sempilador_symbol_test_san

bench-symbol: | $(BUILD_DIR)
	$(CC) $(CFLAGS) test/symbol_bench.c symbol/symbol.c lexer/lexer.c -o $(BUILD_DIR)/sempilador_symbol_bench
	$(BUILD_DIR)/sempilador_symbol_bench

.PHONY: test-build

test-build: build
	$(BUILD_DIR)/sempilador test/valid.csim > $(BUILD_DIR)/test_build.out
	grep -q "programa v" $(BUILD_DIR)/test_build.out
	! $(BUILD_DIR)/sempilador test/symbol.csim > $(BUILD_DIR)/test_build_error.out
	grep -q "Linha 4, coluna 1: esperado" $(BUILD_DIR)/test_build_error.out
	grep -q "contador" $(BUILD_DIR)/test_build_error.out
