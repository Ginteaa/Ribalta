# Ribalta — build
#   make            build di sviluppo        make V=1   mostra i comandi completi
#   make release    build ottimizzata        make test  test unitari (senza display)
#   make install    installa in ~/.local     make uninstall  (le statistiche restano)
#   make debug      ASan + UBSan             make icons      rigenera le PNG dell'icona

CC      ?= cc
PKGS    := sdl3 sdl3-ttf
CFLAGS  ?= -std=c17 -Wall -Wextra -Wpedantic -O2
override CFLAGS += $(shell pkg-config --cflags $(PKGS)) -D_POSIX_C_SOURCE=200809L -Isrc
override LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm
SRC     := $(shell find src -name '*.c' | sort)
OBJ     := $(SRC:src/%.c=build/%.o)
BIN     := ribalta
PREFIX  ?= $(HOME)/.local

# output sintetico ("  CC  core/timer.c"); V=1 per i comandi completi
ifeq ($(V),1)
  Q :=
  say = @true
else
  Q := @
  say = @printf '  %-4s %s\n' $(1) $(2)
endif

all: $(BIN)

$(BIN): $(OBJ)
	$(call say,LD,$@)
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c
	$(call say,CC,$*.c)
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

debug: CFLAGS += -O0 -g -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean
	@$(MAKE) --no-print-directory CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" $(BIN)

release: CFLAGS += -flto -DNDEBUG
release: LDFLAGS += -flto -s
release: clean
	@$(MAKE) --no-print-directory CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" $(BIN)

# installazione con output leggibile (banner, barra di avanzamento): vedi tools/install.sh
install:
	@./tools/install.sh "$(PREFIX)"

# rimuove solo i file installati: configurazione e statistiche dell'utente restano
uninstall:
	@./tools/uninstall.sh "$(PREFIX)"

clean:
	@rm -rf build $(BIN)

-include $(OBJ:.o=.d)

# test unitari (senza display): ogni tests/test_*.c è un programma a sé, fallisce al primo errore
TESTS       := $(patsubst tests/%.c,%,$(wildcard tests/test_*.c))
TEST_COMMON := src/core/settings.c src/core/color.c src/core/timer.c src/platform/paths.c

build/tests/%: tests/%.c $(TEST_COMMON)
	$(call say,CC,tests/$*.c)
	@mkdir -p build/tests
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(TEST_COMMON) $(LDLIBS)

test: $(addprefix build/tests/,$(TESTS))
	@for t in $(TESTS); do printf '  %-4s %s ' TEST $$t; ./build/tests/$$t || exit 1; done

# rigenera le PNG dell'icona da assets/ribalta.svg (serve rsvg-convert; le PNG sono nel repo)
icons:
	./tools/icons.sh

.PHONY: all debug release install uninstall clean icons test
