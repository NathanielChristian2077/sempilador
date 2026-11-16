.DEFAULT_GOAL := build
CC = gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
BUILD_DIR ?= bin
LLI ?= lli
OPT ?= opt
RE2C ?= re2c
PYTHON ?= python3

ifeq ($(OS),Windows_NT)
EXEEXT ?= .exe
ifeq ($(origin SHELL),default)
SHELL := cmd.exe
endif
else
EXEEXT ?=
endif
CMD_SHELL := $(filter cmd cmd.exe CMD CMD.EXE,$(notdir $(subst \,/,$(SHELL))))
ifneq ($(CMD_SHELL),)
.SHELLFLAGS := /C
mkdir = if not exist "$(subst /,\,$(1))" mkdir "$(subst /,\,$(1))"
rmdir = if exist "$(subst /,\,$(1))" rmdir /S /Q "$(subst /,\,$(1))"
executable = "$(subst /,\,$(1))"
else
mkdir = mkdir -p "$(1)"
rmdir = rm -rf -- "$(1)"
executable = "$(1)"
endif
# Checked-in output and CI use re2c 4.1. Suppress dates, versions and path directives.
RE2CFLAGS = --no-generation-date --no-version --no-debug-info -W -Werror

LEXER = lexer/lexer.c
PARSER = parser/parser.c parser/parser_stream.c parser/parser_stack.c \
         parser/parser_recovery.c parser/generated/ll1_tables.c \
         parser/ast.c parser/parser_ast.c
SEMANTIC = symbol/semantic.c type/type.c
SEMANTIC_ANALYSIS = semantic/semantic_model.c semantic/semantic_check.c semantic/semantic_constants.c
CODEGEN = codegen/codegen.c
SOURCES = source/source.c token/token.c $(LEXER) $(PARSER) symbol/symbol.c $(SEMANTIC) \
          $(SEMANTIC_ANALYSIS) $(CODEGEN)
HEADERS = $(wildcard source/*.h token/*.h lexer/*.h parser/*.h parser/generated/*.h symbol/*.h type/*.h \
                     semantic/*.h codegen/*.h)
SANITIZER_FLAGS = -std=c11 -g -O1 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: build run run-llvm clean regen-lexer check-generated test test-source test-token \
        test-lexer test-lexer-contract test-parser test-parser-integration \
        test-parser-diagnostics test-parser-recovery test-parser-stream test-symbol test-type \
        test-semantic-symbol test-build test-sanitize test-parser-sanitize \
        test-symbol-sanitize bench-symbol

build: $(BUILD_DIR)/sempilador$(EXEEXT)

$(BUILD_DIR)/sempilador$(EXEEXT): main.c $(SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(SOURCES) $(LDFLAGS) $(LDLIBS) -lm -o "$@"

run: build
	$(call executable,$(BUILD_DIR)/sempilador$(EXEEXT)) "$(or $(FILE),test/valid.csim)"

# Compiles FILE to LLVM IR and runs it with lli.
run-llvm: build
	$(call executable,$(BUILD_DIR)/sempilador$(EXEEXT)) "$(or $(FILE),test/valid.csim)" "$(BUILD_DIR)/programa.ll"
	"$(LLI)" "$(BUILD_DIR)/programa.ll"

$(BUILD_DIR):
	$(call mkdir,$(BUILD_DIR))

# Builds consume the checked-in lexer; regeneration is always explicit.
$(LEXER):
	$(error Missing $(LEXER). Restore it or run make regen-lexer)

regen-lexer:
	$(PYTHON) -B tools/checks.py lexer --output $(LEXER) -- "$(RE2C)" $(RE2CFLAGS)

check-generated:
	$(PYTHON) -B tools/checks.py lexer --output $(LEXER) --check -- "$(RE2C)" $(RE2CFLAGS)

clean:
	$(call rmdir,$(BUILD_DIR))

.PHONY: test-core test-codegen-unit test-build-system
test: test-core test-codegen

test-core: test-source test-token test-lexer test-lexer-contract test-parser \
           test-parser-integration test-parser-diagnostics test-parser-recovery \
           test-symbol test-type test-semantic-symbol test-build test-parser-stream \
           test-parser-tables test-parser-generated test-codegen-unit

$(BUILD_DIR)/source_test$(EXEEXT): test/source_test.c source/source.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/source_test.c source/source.c $(LDFLAGS) $(LDLIBS) -o "$@"
test-source: $(BUILD_DIR)/source_test$(EXEEXT)
	$(call executable,$<)

$(BUILD_DIR)/token_test$(EXEEXT): test/token_test.c token/token.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/token_test.c token/token.c $(LDFLAGS) $(LDLIBS) -o "$@"
test-token: $(BUILD_DIR)/token_test$(EXEEXT)
	$(call executable,$<)

$(BUILD_DIR)/lexer_test$(EXEEXT): test/lexer_test.c $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/lexer_test.c $(LEXER) $(LDFLAGS) $(LDLIBS) -o "$@"
test-lexer: $(BUILD_DIR)/lexer_test$(EXEEXT)
	$(call executable,$<)

$(BUILD_DIR)/lexer_contract_test$(EXEEXT): test/lexer_contract_test.c $(LEXER) $(PARSER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/lexer_contract_test.c $(LEXER) $(PARSER) $(LDFLAGS) $(LDLIBS) -o "$@"
test-lexer-contract: $(BUILD_DIR)/lexer_contract_test$(EXEEXT)
	$(call executable,$<)

$(BUILD_DIR)/parser_test$(EXEEXT): test/parser_test.c $(PARSER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/parser_test.c $(PARSER) $(LDFLAGS) $(LDLIBS) -o "$@"
test-parser: $(BUILD_DIR)/parser_test$(EXEEXT)
	$(call executable,$<)

$(BUILD_DIR)/parser_integration_test$(EXEEXT): test/parser_integration_test.c $(PARSER) $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/parser_integration_test.c $(PARSER) $(LEXER) $(LDFLAGS) $(LDLIBS) -o "$@"
test-parser-integration: $(BUILD_DIR)/parser_integration_test$(EXEEXT)
	$(call executable,$<)

$(BUILD_DIR)/parser_diagnostic_test$(EXEEXT): test/parser_diagnostic_test.c $(PARSER) $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/parser_diagnostic_test.c $(PARSER) $(LEXER) $(LDFLAGS) $(LDLIBS) -o "$@"
test-parser-diagnostics: $(BUILD_DIR)/parser_diagnostic_test$(EXEEXT)
	$(call executable,$<)

$(BUILD_DIR)/parser_recovery_test$(EXEEXT): test/parser_recovery_test.c $(PARSER) $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/parser_recovery_test.c $(PARSER) $(LEXER) $(LDFLAGS) $(LDLIBS) -o "$@"
test-parser-recovery: $(BUILD_DIR)/parser_recovery_test$(EXEEXT)
	$(call executable,$<)

$(BUILD_DIR)/symbol_test$(EXEEXT): test/symbol_test.c symbol/symbol.c $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/symbol_test.c symbol/symbol.c $(LEXER) $(LDFLAGS) $(LDLIBS) -o "$@"
test-symbol: $(BUILD_DIR)/symbol_test$(EXEEXT)
	$(call executable,$<)

$(BUILD_DIR)/type_test$(EXEEXT): test/type_test.c type/type.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/type_test.c type/type.c $(LDFLAGS) $(LDLIBS) -o "$@"
test-type: $(BUILD_DIR)/type_test$(EXEEXT)
	$(call executable,$<)

$(BUILD_DIR)/semantic_symbol_test$(EXEEXT): test/semantic_symbol_test.c $(SEMANTIC) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/semantic_symbol_test.c $(SEMANTIC) $(LDFLAGS) $(LDLIBS) -o "$@"
test-semantic-symbol: $(BUILD_DIR)/semantic_symbol_test$(EXEEXT)
	$(call executable,$<)

test-build: build
	$(PYTHON) -B tools/checks.py cli --compiler "$(BUILD_DIR)/sempilador$(EXEEXT)" --directory "$(BUILD_DIR)"

test-sanitize test-parser-sanitize test-symbol-sanitize test-semantic-sanitize: export ASAN_OPTIONS := $(if $(ASAN_OPTIONS),$(ASAN_OPTIONS):)halt_on_error=1:exitcode=99
test-sanitize test-parser-sanitize test-symbol-sanitize test-semantic-sanitize: export UBSAN_OPTIONS := $(if $(UBSAN_OPTIONS),$(UBSAN_OPTIONS):)halt_on_error=1:exitcode=98

test-sanitize:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sanitize CFLAGS="$(SANITIZER_FLAGS)" test

test-parser-sanitize:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sanitize CFLAGS="$(SANITIZER_FLAGS)" \
		test-parser test-parser-integration test-parser-diagnostics test-parser-recovery test-parser-stream test-ast test-ast-resource

test-symbol-sanitize:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sanitize CFLAGS="$(SANITIZER_FLAGS)" test-symbol test-semantic-symbol

$(BUILD_DIR)/symbol_bench$(EXEEXT): test/symbol_bench.c symbol/symbol.c $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) test/symbol_bench.c symbol/symbol.c $(LEXER) $(LDFLAGS) $(LDLIBS) -o "$@"
bench-symbol: $(BUILD_DIR)/symbol_bench$(EXEEXT)
	$(call executable,$<)

.PHONY: regen-parser check-parser-generated test-parser-generated test-parser-stream
regen-parser:
	$(PYTHON) -B parser/tools/generate_ll1.py

check-parser-generated:
	$(PYTHON) -B parser/tools/generate_ll1.py --check

test-parser-generated: check-parser-generated
	$(PYTHON) -B -m unittest discover -s test -p "parser_generation_test.py" -v

$(BUILD_DIR)/parser_stream_test$(EXEEXT): test/parser_stream_test.c $(PARSER) $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/parser_stream_test.c $(PARSER) $(LEXER) $(LDFLAGS) $(LDLIBS) -o "$@"
test-parser-stream: $(BUILD_DIR)/parser_stream_test$(EXEEXT)
	$(call executable,$<)

.PHONY: test-parser-tables
$(BUILD_DIR)/parser_table_test$(EXEEXT): test/parser_table_test.c parser/generated/ll1_tables.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/parser_table_test.c parser/generated/ll1_tables.c $(LDFLAGS) $(LDLIBS) -o "$@"
test-parser-tables: $(BUILD_DIR)/parser_table_test$(EXEEXT)
	$(call executable,$<)

.PHONY: test-ast
test-core: test-ast
$(BUILD_DIR)/ast_test$(EXEEXT): test/ast_test.c $(PARSER) $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/ast_test.c $(PARSER) $(LEXER) $(LDFLAGS) $(LDLIBS) -o "$@"
test-ast: $(BUILD_DIR)/ast_test$(EXEEXT)
	$(call executable,$<)

.PHONY: test-ast-resource
test-core: test-ast-resource
$(BUILD_DIR)/ast_resource_test$(EXEEXT): test/ast_resource_test.c $(PARSER) $(LEXER) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/ast_resource_test.c $(PARSER) $(LEXER) $(LDFLAGS) $(LDLIBS) -o "$@"
test-ast-resource: $(BUILD_DIR)/ast_resource_test$(EXEEXT)
	$(call executable,$<)

SEMANTIC_HEADERS = $(wildcard semantic/*.h)
.PHONY: test-semantic-analysis
test-core: test-semantic-analysis
$(BUILD_DIR)/semantic_analysis_test$(EXEEXT): test/semantic_analysis_test.c $(SEMANTIC_ANALYSIS) $(SEMANTIC_HEADERS) $(PARSER) $(LEXER) $(SEMANTIC) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/semantic_analysis_test.c $(SEMANTIC_ANALYSIS) $(PARSER) $(LEXER) $(SEMANTIC) $(LDFLAGS) $(LDLIBS) -lm -o "$@"
test-semantic-analysis: $(BUILD_DIR)/semantic_analysis_test$(EXEEXT)
	$(call executable,$<)

.PHONY: test-semantic-resources test-semantic-sanitize
test-core: test-semantic-resources
$(BUILD_DIR)/semantic_resource_test$(EXEEXT): test/semantic_resource_test.c $(SEMANTIC_ANALYSIS) $(SEMANTIC_HEADERS) $(PARSER) $(LEXER) $(SEMANTIC) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/semantic_resource_test.c $(SEMANTIC_ANALYSIS) $(PARSER) $(LEXER) $(SEMANTIC) $(LDFLAGS) $(LDLIBS) -lm -o "$@"
test-semantic-resources: $(BUILD_DIR)/semantic_resource_test$(EXEEXT)
	$(call executable,$<)
test-semantic-sanitize:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sanitize CFLAGS="$(SANITIZER_FLAGS)" test-semantic-analysis test-semantic-resources

# Each test/codegen/NAME.csim is compiled, checked by opt, run by lli with
# NAME.in as input (if present) and compared with NAME.out.
.PHONY: test-codegen
$(BUILD_DIR)/codegen_test$(EXEEXT): test/codegen_test.c $(CODEGEN) $(SEMANTIC_ANALYSIS) $(SEMANTIC_HEADERS) $(PARSER) $(LEXER) $(SEMANTIC) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG test/codegen_test.c $(CODEGEN) $(SEMANTIC_ANALYSIS) $(PARSER) $(LEXER) $(SEMANTIC) $(LDFLAGS) $(LDLIBS) -lm -o "$@"
test-codegen-unit: $(BUILD_DIR)/codegen_test$(EXEEXT)
	$(call executable,$<)

test-codegen: test-codegen-unit build
	$(PYTHON) -B tools/checks.py codegen --compiler "$(BUILD_DIR)/sempilador$(EXEEXT)" --directory "$(BUILD_DIR)" --opt "$(OPT)" --lli "$(LLI)"

test-build-system:
	$(PYTHON) -B test/makefile_test.py --make "$(MAKE)" --cc "$(CC)" --cflags="$(CFLAGS)" --shell "$(SHELL)"
