# Ribalta — build
#   make            build di sviluppo        make V=1   mostra i comandi completi
#   make release    build ottimizzata        make test  test unitari (senza display)
#   make install    installa in ~/.local     make uninstall  (le statistiche restano)
#   make debug      ASan + UBSan             make icons      rigenera le PNG dell'icona
#   make app        macOS: build/Ribalta.app  (make install su macOS la copia in ~/Applications)

CC      ?= cc
PKGS    := sdl3 sdl3-ttf
# PKGFLAGS=--static quando SDL3_ttf è una libreria statica (Flatpak): aggiunge le sue dipendenze
PKGFLAGS ?=
CFLAGS  ?= -std=c17 -Wall -Wextra -Wpedantic -O2
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  # macOS: elenco font con CoreText (fonts_mac.c); _DARWIN_C_SOURCE espone anche flock
  FCPKG      :=
  PLATDEF    := -D_DARWIN_C_SOURCE
  PLATLIBS   := -framework CoreText -framework CoreFoundation
  FONTS_SRC  := src/platform/fonts_mac.c
  STRIP_LD   := -Wl,-dead_strip
else
  # fontconfig: solo header (caricata a richiesta con dlopen in fonts.c), non va linkata
  FCPKG      := fontconfig
  PLATDEF    := -D_POSIX_C_SOURCE=200809L
  PLATLIBS   :=
  FONTS_SRC  := src/platform/fonts.c
  STRIP_LD   := -s
endif

override CFLAGS += $(shell pkg-config --cflags $(PKGS) $(FCPKG)) $(PLATDEF) -Isrc
override LDLIBS += $(shell pkg-config $(PKGFLAGS) --libs $(PKGS)) -lm $(PLATLIBS)
# fonts.c e fonts_mac.c sono alternativi: si compila solo quello della piattaforma
SRC     := $(filter-out src/platform/fonts.c src/platform/fonts_mac.c,$(shell find src -name '*.c')) $(FONTS_SRC)
SRC     := $(sort $(SRC))
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
release: LDFLAGS += -flto $(STRIP_LD)
release: clean
	@$(MAKE) --no-print-directory CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" $(BIN)

# installazione con output leggibile (banner, barra di avanzamento): vedi tools/install.sh
ifeq ($(UNAME_S),Darwin)
# macOS: Ribalta.app (vedi tools/macos-app.sh) copiata in ~/Applications
APPDIR  ?= $(HOME)/Applications
app: release
	@./tools/macos-app.sh

install: app
	@mkdir -p "$(APPDIR)"
	@rm -rf "$(APPDIR)/Ribalta.app"
	@cp -R build/Ribalta.app "$(APPDIR)/"
	@echo "Ribalta.app installata in $(APPDIR)"
else
app:
	@echo "make app: solo su macOS" >&2; exit 1

install:
	@./tools/install.sh "$(PREFIX)"
endif

# installazione semplice e silenziosa per chi impacchetta (Flatpak, distribuzioni):
#   make install-files PREFIX=/app DESTDIR=... APPID=io.github.ginteaa.Ribalta
# APPID rinomina .desktop e icone; vuoto = "ribalta". Non compila: serve prima make / make release.
APPID   ?= ribalta
ICON_SIZES := 16 22 24 32 48 64 128 256 512
install-files:
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	for f in font.ttf OFL.txt ding.wav ribalta.svg; do \
	  install -Dm644 assets/$$f $(DESTDIR)$(PREFIX)/share/ribalta/$$f; done
	for s in $(ICON_SIZES); do \
	  install -Dm644 assets/icons/$${s}x$${s}.png \
	    $(DESTDIR)$(PREFIX)/share/icons/hicolor/$${s}x$${s}/apps/$(APPID).png; done
	install -Dm644 assets/ribalta.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/$(APPID).svg
	install -d $(DESTDIR)$(PREFIX)/share/applications
	sed -e 's/^Icon=.*/Icon=$(APPID)/' -e 's/^StartupWMClass=.*/StartupWMClass=$(APPID)/' \
	  assets/ribalta.desktop > $(DESTDIR)$(PREFIX)/share/applications/$(APPID).desktop

# rimuove solo i file installati: configurazione e statistiche dell'utente restano
uninstall:
	@./tools/uninstall.sh "$(PREFIX)"

clean:
	@rm -rf build $(BIN)

-include $(OBJ:.o=.d)

# test unitari (senza display): ogni tests/test_*.c è un programma a sé, fallisce al primo errore
TESTS       := $(patsubst tests/%.c,%,$(wildcard tests/test_*.c))
TEST_COMMON := src/core/settings.c src/core/color.c src/core/timer.c src/platform/paths.c \
               src/platform/fonts_common.c $(FONTS_SRC) src/core/stats.c

build/tests/%: tests/%.c $(TEST_COMMON)
	$(call say,CC,tests/$*.c)
	@mkdir -p build/tests
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(TEST_COMMON) $(LDLIBS)

test: $(addprefix build/tests/,$(TESTS))
	@for t in $(TESTS); do printf '  %-4s %s ' TEST $$t; ./build/tests/$$t || exit 1; done

# rigenera le PNG dell'icona da assets/ribalta.svg (serve rsvg-convert; le PNG sono nel repo)
icons:
	./tools/icons.sh

.PHONY: all app debug release install install-files uninstall clean icons test
