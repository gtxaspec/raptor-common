CC ?= gcc
SAN ?= address
CFLAGS = -Wall -Wextra -std=c11 -D_GNU_SOURCE -g -fsanitize=$(SAN) -I../include -I../third_party/monocypher -I.
LDFLAGS = -fsanitize=$(SAN) -lm

TEST_SRCS = main.c test_config.c test_util.c test_json.c test_http.c test_file.c test_sei.c test_jpeg.c
LIB_SRCS = ../src/rss_config.c ../src/rss_util.c ../src/rss_http.c ../src/rss_log.c ../src/rss_sei.c ../src/rss_sign.c ../src/rss_jpeg.c ../src/cJSON.c ../third_party/monocypher/monocypher.c ../third_party/monocypher/monocypher-ed25519.c

tests: $(TEST_SRCS) $(LIB_SRCS) greatest.h
	$(CC) $(CFLAGS) -o $@ $(TEST_SRCS) $(LIB_SRCS) $(LDFLAGS)

test: tests
	./tests

clean:
	rm -f tests

.PHONY: test clean
