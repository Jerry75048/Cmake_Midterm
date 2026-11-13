CC      := gcc
CFLAGS  := -Wall -Wextra -std=gnu11 -D_GNU_SOURCE -Iinclude -fPIC
LDFLAGS := -Llib -Wl,-rpath,'$$ORIGIN/../lib' -lnspslib

BUILD ?= release
ifeq ($(BUILD),debug)
CFLAGS += -g -O0 -DDEBUG_BUILD
else
CFLAGS += -O2
endif

LIB_SRCS   := src/lib/log.c src/lib/netutil.c src/lib/sysinfo.c
LIB_OBJS   := $(LIB_SRCS:.c=.o)
LIB_TARGET := lib/libnspslib.so

BIN_SERVER := bin/server
BIN_CLIENT := bin/client

.PHONY: all debug clean dirs demo-binaries

all: dirs $(LIB_TARGET) $(BIN_SERVER) $(BIN_CLIENT)

debug:
	$(MAKE) BUILD=debug all

dirs:
	mkdir -p lib bin shared uploads

$(LIB_TARGET): $(LIB_OBJS) | dirs
	$(CC) -shared -o $@ $(LIB_OBJS)

src/lib/%.o: src/lib/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BIN_SERVER): src/server.c $(LIB_TARGET) | dirs
	$(CC) $(CFLAGS) -o $@ src/server.c $(LDFLAGS)

$(BIN_CLIENT): src/client.c $(LIB_TARGET) | dirs
	$(CC) $(CFLAGS) -o $@ src/client.c $(LDFLAGS)

# --- robustness demo variants: same source, one guard compiled out at a time ---
bin/server_no_sigchld: CFLAGS += -DDEMO_NO_SIGCHLD
bin/server_no_sigchld: src/server.c $(LIB_TARGET) | dirs
	$(CC) $(CFLAGS) -o $@ src/server.c $(LDFLAGS)

bin/server_no_sigpipe: CFLAGS += -DDEMO_NO_SIGPIPE
bin/server_no_sigpipe: src/server.c $(LIB_TARGET) | dirs
	$(CC) $(CFLAGS) -o $@ src/server.c $(LDFLAGS)

bin/server_no_lencheck: CFLAGS += -DDEMO_NO_LENCHECK
bin/server_no_lencheck: src/server.c $(LIB_TARGET) | dirs
	$(CC) $(CFLAGS) -o $@ src/server.c $(LDFLAGS)

demo-binaries: bin/server_no_sigchld bin/server_no_sigpipe bin/server_no_lencheck

clean:
	rm -f $(LIB_OBJS) $(LIB_TARGET) $(BIN_SERVER) $(BIN_CLIENT) \
	      bin/server_no_sigchld bin/server_no_sigpipe bin/server_no_lencheck
