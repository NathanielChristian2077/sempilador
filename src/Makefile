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
