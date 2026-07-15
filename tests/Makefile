CC ?= gcc
SAN ?= address
CFLAGS = -Wall -Wextra -std=c11 -D_GNU_SOURCE -g -fsanitize=$(SAN) -I../include -I.
LDFLAGS = -fsanitize=$(SAN) -lm

TEST_SRCS = main.c test_config.c test_util.c test_json.c test_http.c test_file.c test_sei.c
LIB_SRCS = ../src/rss_config.c ../src/rss_util.c ../src/rss_http.c ../src/rss_log.c ../src/rss_sei.c ../src/cJSON.c

tests: $(TEST_SRCS) $(LIB_SRCS) greatest.h
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS) $(LIB_SRCS) $(LDFLAGS)

test: tests
	./tests

clean:
	rm -f tests

.PHONY: test clean
