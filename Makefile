CC      ?= cc
PKGS    := sdl3 sdl3-ttf
CFLAGS  ?= -std=c17 -Wall -Wextra -Wpedantic -O2
CFLAGS  += $(shell pkg-config --cflags $(PKGS)) -D_POSIX_C_SOURCE=200809L
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=build/%.o)
BIN     := flipclock
PREFIX  ?= $(HOME)/.local

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

build:
	mkdir -p build

debug: CFLAGS += -O0 -g -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean
	$(MAKE) CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" $(BIN)

release: CFLAGS += -flto -DNDEBUG
release: LDFLAGS += -flto -s
release: clean
	$(MAKE) CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" $(BIN)

install: release
	install -Dm755 $(BIN) $(PREFIX)/bin/$(BIN)
	install -Dm644 -t $(PREFIX)/share/flipclock assets/*

uninstall:
	rm -rf $(PREFIX)/bin/$(BIN) $(PREFIX)/share/flipclock

clean:
	rm -rf build $(BIN)

-include $(OBJ:.o=.d)
.PHONY: all debug release install uninstall clean
