.DEFAULT_GOAL := build
CC = gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
BUILD_DIR ?= bin
RE2C ?= re2c
# Checked-in output and CI use re2c 4.1. Suppress dates, versions and path directives.
RE2CFLAGS = --no-generation-date --no-version --no-debug-info -W -Werror

LEXER = lexer/lexer.c
PARSER = parser/parser.c parser/parser_stream.c parser/parser_stack.c \
         parser/parser_recovery.c parser/generated/ll1_tables.c \
         parser/ast.c parser/parser_ast.c
SEMANTIC = symbol/semantic.c type/type.c
SOURCES = source/source.c token/token.c $(LEXER) $(PARSER) symbol/symbol.c $(SEMANTIC)
HEADERS = $(wildcard source/*.h token/*.h lexer/*.h parser/*.h parser/generated/*.h symbol/*.h type/*.h)
SANITIZER_FLAGS = -std=c11 -g -O1 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: build run clean regen-lexer check-generated test test-source test-token \
        test-lexer test-lexer-contract test-parser test-parser-integration \
        test-parser-diagnostics test-parser-recovery test-parser-stream test-symbol test-type \
        test-semantic-symbol test-build test-sanitize test-parser-sanitize \
        test-symbol-sanitize bench-symbol

build: $(BUILD_DIR)/sempilador

$(BUILD_DIR)/sempilador: main.c $(SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(SOURCES) $(LDFLAGS) $(LDLIBS) -o $@

run: build
	$(BUILD_DIR)/sempilador $(or $(FILE),test/valid.csim)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(LEXER): lexer/lexer.re
	$(RE2C) $(RE2CFLAGS) -o $@.tmp $<
	mv $@.tmp $@

regen-lexer:
	$(RE2C) $(RE2CFLAGS) -o $(LEXER).tmp lexer/lexer.re
	mv $(LEXER).tmp $(LEXER)

check-generated: | $(BUILD_DIR)
	$(RE2C) $(RE2CFLAGS) -o $(BUILD_DIR)/lexer.check.c lexer/lexer.re
	diff -u $(LEXER) $(BUILD_DIR)/lexer.check.c

clean:
	rm -rf $(BUILD_DIR)

test: test-source test-token test-lexer test-lexer-contract test-parser \
      test-parser-integration test-parser-diagnostics test-parser-recovery \
      test-symbol test-type test-semantic-symbol test-build test-parser-stream test-parser-tables

$(BUILD_DIR)/source_test: test/source_test.c source/source.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/source_test.c source/source.c $(LDFLAGS) $(LDLIBS) -o $@
test-source: $(BUILD_DIR)/source_test
	$<

$(BUILD_DIR)/token_test: test/token_test.c token/token.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/token_test.c token/token.c $(LDFLAGS) $(LDLIBS) -o $@
test-token: $(BUILD_DIR)/token_test
	$<

$(BUILD_DIR)/lexer_test: test/lexer_test.c $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/lexer_test.c $(LEXER) $(LDFLAGS) $(LDLIBS) -o $@
test-lexer: $(BUILD_DIR)/lexer_test
	$<

$(BUILD_DIR)/lexer_contract_test: test/lexer_contract_test.c $(LEXER) $(PARSER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/lexer_contract_test.c $(LEXER) $(PARSER) $(LDFLAGS) $(LDLIBS) -o $@
test-lexer-contract: $(BUILD_DIR)/lexer_contract_test
	$<

$(BUILD_DIR)/parser_test: test/parser_test.c $(PARSER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/parser_test.c $(PARSER) $(LDFLAGS) $(LDLIBS) -o $@
test-parser: $(BUILD_DIR)/parser_test
	$<

$(BUILD_DIR)/parser_integration_test: test/parser_integration_test.c $(PARSER) $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/parser_integration_test.c $(PARSER) $(LEXER) $(LDFLAGS) $(LDLIBS) -o $@
test-parser-integration: $(BUILD_DIR)/parser_integration_test
	$<

$(BUILD_DIR)/parser_diagnostic_test: test/parser_diagnostic_test.c $(PARSER) $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/parser_diagnostic_test.c $(PARSER) $(LEXER) $(LDFLAGS) $(LDLIBS) -o $@
test-parser-diagnostics: $(BUILD_DIR)/parser_diagnostic_test
	$<

$(BUILD_DIR)/parser_recovery_test: test/parser_recovery_test.c $(PARSER) $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/parser_recovery_test.c $(PARSER) $(LEXER) $(LDFLAGS) $(LDLIBS) -o $@
test-parser-recovery: $(BUILD_DIR)/parser_recovery_test
	$<

$(BUILD_DIR)/symbol_test: test/symbol_test.c symbol/symbol.c $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/symbol_test.c symbol/symbol.c $(LEXER) $(LDFLAGS) $(LDLIBS) -o $@
test-symbol: $(BUILD_DIR)/symbol_test
	$<

$(BUILD_DIR)/type_test: test/type_test.c type/type.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/type_test.c type/type.c $(LDFLAGS) $(LDLIBS) -o $@
test-type: $(BUILD_DIR)/type_test
	$<

$(BUILD_DIR)/semantic_symbol_test: test/semantic_symbol_test.c $(SEMANTIC) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/semantic_symbol_test.c $(SEMANTIC) $(LDFLAGS) $(LDLIBS) -o $@
test-semantic-symbol: $(BUILD_DIR)/semantic_symbol_test
	$<

test-build: build
	$(BUILD_DIR)/sempilador test/valid.csim > $(BUILD_DIR)/test_build.out
	grep -q "programa v" $(BUILD_DIR)/test_build.out
	@status=0; $(BUILD_DIR)/sempilador test/symbol.csim > $(BUILD_DIR)/test_build_error.out || status=$$?; test "$$status" -eq 1
	grep -q "Linha 4, coluna 1: esperado" $(BUILD_DIR)/test_build_error.out
	grep -q "contador" $(BUILD_DIR)/test_build_error.out
	@status=0; $(BUILD_DIR)/sempilador test/invalid_number.csim > $(BUILD_DIR)/test_invalid_number.out || status=$$?; test "$$status" -eq 1
	grep -Fq 'Linha 1, coluna 12: erro léxico' $(BUILD_DIR)/test_invalid_number.out
	grep -Fq 'encontrado token inválido "2D"' $(BUILD_DIR)/test_invalid_number.out

test-sanitize:
	ASAN_OPTIONS="$${ASAN_OPTIONS:+$$ASAN_OPTIONS:}halt_on_error=1:exitcode=99" \
	UBSAN_OPTIONS="$${UBSAN_OPTIONS:+$$UBSAN_OPTIONS:}halt_on_error=1:exitcode=98" \
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sanitize CFLAGS="$(SANITIZER_FLAGS)" test

test-parser-sanitize:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sanitize CFLAGS="$(SANITIZER_FLAGS)" \
		test-parser test-parser-integration test-parser-diagnostics test-parser-recovery test-parser-stream test-ast test-ast-resource

test-symbol-sanitize:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sanitize CFLAGS="$(SANITIZER_FLAGS)" test-symbol test-semantic-symbol

$(BUILD_DIR)/symbol_bench: test/symbol_bench.c symbol/symbol.c $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) test/symbol_bench.c symbol/symbol.c $(LEXER) $(LDFLAGS) $(LDLIBS) -o $@
bench-symbol: $(BUILD_DIR)/symbol_bench
	$<

.PHONY: regen-parser check-parser-generated test-parser-generated test-parser-stream
regen-parser:
	python3 parser/tools/generate_ll1.py

check-parser-generated:
	python3 parser/tools/generate_ll1.py --check

test-parser-generated: check-parser-generated
	PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s test -p 'parser_generation_test.py' -v

$(BUILD_DIR)/parser_stream_test: test/parser_stream_test.c $(PARSER) $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/parser_stream_test.c $(PARSER) $(LEXER) $(LDFLAGS) $(LDLIBS) -o $@
test-parser-stream: $(BUILD_DIR)/parser_stream_test
	$<

.PHONY: test-parser-tables
$(BUILD_DIR)/parser_table_test: test/parser_table_test.c parser/generated/ll1_tables.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/parser_table_test.c parser/generated/ll1_tables.c $(LDFLAGS) $(LDLIBS) -o $@
test-parser-tables: $(BUILD_DIR)/parser_table_test
	$<

.PHONY: test-ast
test: test-ast
$(BUILD_DIR)/ast_test: test/ast_test.c $(PARSER) $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/ast_test.c $(PARSER) $(LEXER) $(LDFLAGS) $(LDLIBS) -o $@
test-ast: $(BUILD_DIR)/ast_test
	$<

.PHONY: test-ast-resource
test: test-ast-resource
$(BUILD_DIR)/ast_resource_test: test/ast_resource_test.c $(PARSER) $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/ast_resource_test.c $(PARSER) $(LEXER) $(LDFLAGS) $(LDLIBS) -o $@
test-ast-resource: $(BUILD_DIR)/ast_resource_test
	$<

SEMANTIC_ANALYSIS = semantic/semantic_model.c semantic/semantic_check.c semantic/semantic_constants.c
SEMANTIC_HEADERS = $(wildcard semantic/*.h)
.PHONY: test-semantic-analysis
test: test-semantic-analysis
$(BUILD_DIR)/semantic_analysis_test: test/semantic_analysis_test.c $(SEMANTIC_ANALYSIS) $(SEMANTIC_HEADERS) $(PARSER) $(LEXER) $(SEMANTIC) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/semantic_analysis_test.c $(SEMANTIC_ANALYSIS) $(PARSER) $(LEXER) $(SEMANTIC) $(LDFLAGS) $(LDLIBS) -lm -o $@
test-semantic-analysis: $(BUILD_DIR)/semantic_analysis_test
	$<

.PHONY: test-semantic-resources test-semantic-sanitize
test: test-semantic-resources
$(BUILD_DIR)/semantic_resource_test: test/semantic_resource_test.c $(SEMANTIC_ANALYSIS) $(SEMANTIC_HEADERS) $(PARSER) $(LEXER) $(SEMANTIC) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/semantic_resource_test.c $(SEMANTIC_ANALYSIS) $(PARSER) $(LEXER) $(SEMANTIC) $(LDFLAGS) $(LDLIBS) -lm -o $@
test-semantic-resources: $(BUILD_DIR)/semantic_resource_test
	$<
test-semantic-sanitize:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sanitize CFLAGS="$(SANITIZER_FLAGS)" test-semantic-analysis test-semantic-resources
