CC      ?= gcc
CFLAGS  := -Wall -Wextra -std=c11 -D_GNU_SOURCE -Os -fPIC
CFLAGS  += -ffunction-sections -fdata-sections
CFLAGS  += -fno-asynchronous-unwind-tables -fmerge-all-constants -fno-ident
CFLAGS  += -Iinclude
SRCS    := src/rss_log.c src/rss_config.c src/rss_daemon.c src/rss_util.c src/rss_ctrl_cmds.c src/rss_http.c src/cJSON.c
OBJS    := $(SRCS:.c=.o)
LIB     := librss_common.so

all: $(LIB)

$(LIB): $(OBJS)
	$(CC) -shared -Wl,-soname,librss_common.so -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB)

.PHONY: all clean
