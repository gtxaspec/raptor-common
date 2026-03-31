CC      ?= gcc
AR      ?= ar
CFLAGS  := -Wall -Wextra -std=c11 -D_GNU_SOURCE -Os
CFLAGS  += -Iinclude
SRCS    := src/rss_log.c src/rss_config.c src/rss_daemon.c src/rss_util.c src/rss_ctrl.c src/rss_http.c
OBJS    := $(SRCS:.c=.o)
LIB     := librss_common.a

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(LIB)

.PHONY: all clean
