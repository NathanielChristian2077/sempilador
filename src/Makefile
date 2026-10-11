.PHONY: test

test:
	@gcc -Wall -Wextra -std=c11 -Isource \
		./test/source_test.c ./source/source.c \
		-o /tmp/source_test
	@/tmp/source_test
