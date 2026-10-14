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


.PHONY: test-parser test-parser-integration test-parser-diagnostics test-parser-recovery test-parser-sanitize

test: test-parser test-parser-integration test-parser-diagnostics test-parser-recovery

test-parser:
	$(CC) $(CFLAGS) test/parser_test.c parser/parser.c -o /tmp/sempilador_parser_test
	/tmp/sempilador_parser_test

test-parser-integration:
	$(CC) $(CFLAGS) test/parser_integration_test.c parser/parser.c lexer/lexer.c -o /tmp/sempilador_parser_integration_test
	/tmp/sempilador_parser_integration_test

test-parser-diagnostics:
	$(CC) $(CFLAGS) test/parser_diagnostic_test.c parser/parser.c lexer/lexer.c -o /tmp/sempilador_parser_diagnostic_test
	/tmp/sempilador_parser_diagnostic_test

test-parser-recovery:
	$(CC) $(CFLAGS) test/parser_recovery_test.c parser/parser.c lexer/lexer.c -o /tmp/sempilador_parser_recovery_test
	/tmp/sempilador_parser_recovery_test

PARSER_SAN_FLAGS = -std=c11 -g -O1 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer

test-parser-sanitize:
	$(CC) $(PARSER_SAN_FLAGS) test/parser_test.c parser/parser.c -o /tmp/sempilador_parser_test_san
	/tmp/sempilador_parser_test_san
	$(CC) $(PARSER_SAN_FLAGS) test/parser_integration_test.c parser/parser.c lexer/lexer.c -o /tmp/sempilador_parser_integration_test_san
	/tmp/sempilador_parser_integration_test_san
	$(CC) $(PARSER_SAN_FLAGS) test/parser_diagnostic_test.c parser/parser.c lexer/lexer.c -o /tmp/sempilador_parser_diagnostic_test_san
	/tmp/sempilador_parser_diagnostic_test_san
	$(CC) $(PARSER_SAN_FLAGS) test/parser_recovery_test.c parser/parser.c lexer/lexer.c -o /tmp/sempilador_parser_recovery_test_san
	/tmp/sempilador_parser_recovery_test_san
