CC      ?= gcc
AR      ?= ar
CFLAGS  := -Wall -Wextra -std=c11 -D_GNU_SOURCE -Os -fPIC
CFLAGS  += -ffunction-sections -fdata-sections
CFLAGS  += -fno-asynchronous-unwind-tables -fmerge-all-constants -fno-ident
CFLAGS  += -Iinclude
SRCS    := src/rss_log.c src/rss_config.c src/rss_daemon.c src/rss_util.c src/rss_ctrl_cmds.c src/rss_http.c src/cJSON.c
OBJS    := $(SRCS:.c=.o)
LIB_SO  := librss_common.so
LIB_A   := librss_common.a

all: $(LIB_SO) $(LIB_A)

$(LIB_SO): $(OBJS)
	$(CC) -shared -Wl,-soname,librss_common.so -Wl,-Bsymbolic -o $@ $^

$(LIB_A): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB_SO) $(LIB_A)

.PHONY: all clean
