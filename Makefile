# Ribalta — build
#   make            build di sviluppo        make V=1   mostra i comandi completi
#   make release    build ottimizzata        make test  test unitari (senza display)
#   make install    installa in ~/.local     make uninstall  (le statistiche restano)
#   make debug      ASan + UBSan             make icons      rigenera le icone (PNG e assets/ribalta.ico)
#   make app        macOS: build/Ribalta.app  (make install su macOS la copia in ~/Applications)
#   make dist       Windows (MSYS2 UCRT64): build/Ribalta-windows-x86_64.zip, cartella portabile con le DLL

CC      ?= cc
PKGS    := sdl3 sdl3-ttf
# PKGFLAGS=--static quando SDL3_ttf è una libreria statica (Flatpak): aggiunge le sue dipendenze
PKGFLAGS ?=
CFLAGS  ?= -std=c17 -Wall -Wextra -Wpedantic -O2
UNAME_S := $(shell uname -s)
# EXE: suffisso dell'eseguibile (.exe solo su Windows); GUILD: opzioni di link solo per l'app, non per i test;
# RES_OBJ: risorse dell'exe (icona, manifest, versione), compilate con windres solo su Windows
EXE     :=
GUILD   :=
RES_OBJ :=

ifeq ($(OS),Windows_NT)
  # Windows (MSYS2 MinGW-w64 UCRT64; OS e' definita in ogni ambiente Windows, MSYS2 compreso):
  # niente fontconfig, elenco font in fonts_win.c. PLATDEF: solo __USE_MINGW_ANSI_STDIO=1, cosi'
  # printf/snprintf hanno la semantica C99 di Linux (%zu, snprintf che ritorna la lunghezza anche
  # se tronca) e gcc controlla i formati senza avvisi con -std=c17 -Wpedantic. Non serve
  # _POSIX_C_SOURCE: su Windows il codice non usa API POSIX (vedi platform/compat.h).
  # -mwindows: sottosistema GUI (niente finestra di console); le librerie di Windows che servono
  # (kernel32, user32, advapi32...) sono gia' nell'elenco predefinito di gcc.
  EXE        := .exe
  FCPKG      :=
  PLATDEF    := -D__USE_MINGW_ANSI_STDIO=1
  PLATLIBS   := -ladvapi32
  GUILD      := -mwindows
  # risorse di ribalta.exe: icona, manifest (UTF-8, DPI), versione (ribalta.rc, vedi sotto)
  WINDRES    ?= windres
  RES_OBJ    := build/ribalta.res.o
  FONTS_SRC  := src/platform/fonts_win.c
  STRIP_LD   := -s
else ifeq ($(UNAME_S),Darwin)
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

# versione: unica fonte il file VERSION (funziona anche dal tarball di una release, senza .git)
VERSION := $(shell cat VERSION 2>/dev/null || echo dev)
override CFLAGS += $(shell pkg-config --cflags $(PKGS) $(FCPKG)) $(PLATDEF) -Isrc
# fuori da CFLAGS: debug/release richiamano make passando CFLAGS (si duplicherebbe senza virgolette)
VERSION_DEF := -DRIBALTA_VERSION='"$(VERSION)"'
override LDLIBS += $(shell pkg-config $(PKGFLAGS) --libs $(PKGS)) -lm $(PLATLIBS)
# fonts.c, fonts_mac.c e fonts_win.c sono alternativi: si compila solo quello della piattaforma
SRC     := $(filter-out src/platform/fonts.c src/platform/fonts_mac.c src/platform/fonts_win.c,$(shell find src -name '*.c')) $(FONTS_SRC)
SRC     := $(sort $(SRC))
OBJ     := $(SRC:src/%.c=build/%.o)
BIN     := ribalta$(EXE)
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

$(BIN): $(OBJ) $(RES_OBJ)
	$(call say,LD,$@)
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(GUILD)

ifeq ($(OS),Windows_NT)
# Risorse dell'exe: windres compila packaging/windows/ribalta.rc in un oggetto COFF che si linka
# solo in ribalta.exe (non nei test). La versione arriva dal file VERSION tramite un header
# generato (build/ribalta_version.h): niente -D con virgolette, che windres, passandole al
# preprocessore tramite la shell di Windows, rovina facilmente. VERSION non numerico = 0.0.0.
RC_SRC       := packaging/windows/ribalta.rc
RC_VERSION_H := build/ribalta_version.h
RC_VER       := $(subst ., ,$(VERSION))
ifneq ($(words $(RC_VER)),3)
  RC_VER     := 0 0 0
endif

$(RC_VERSION_H): VERSION
	@mkdir -p $(dir $@)
	@printf '#define RIBALTA_VER_MAJOR %s\n#define RIBALTA_VER_MINOR %s\n#define RIBALTA_VER_PATCH %s\n#define RIBALTA_VERSION_STR "%s"\n' \
	  $(word 1,$(RC_VER)) $(word 2,$(RC_VER)) $(word 3,$(RC_VER)) "$(VERSION)" > $@

$(RES_OBJ): $(RC_SRC) packaging/windows/ribalta.manifest assets/ribalta.ico $(RC_VERSION_H)
	$(call say,RC,ribalta.rc)
	@mkdir -p $(dir $@)
	$(Q)$(WINDRES) -I build -O coff -i $< -o $@
endif

# main.c contiene RIBALTA_VERSION: si ricompila quando cambia VERSION
build/main.o: VERSION

build/%.o: src/%.c
	$(call say,CC,$*.c)
	@mkdir -p $(dir $@)
	$(Q)$(CC) $(CFLAGS) $(VERSION_DEF) -MMD -MP -c -o $@ $<

debug: CFLAGS += -O0 -g -fsanitize=address,undefined
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean
	@$(MAKE) --no-print-directory CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" $(BIN)

release: CFLAGS += -flto -DNDEBUG
release: LDFLAGS += -flto $(STRIP_LD)
release: clean
	@$(MAKE) --no-print-directory CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" $(BIN)

# installazione con output leggibile (banner, barra di avanzamento): vedi tools/install.sh
ifeq ($(OS),Windows_NT)
# Windows: installer e disinstallatore arriveranno con i pacchetti; per ora solo make dist (zip portabile)
app install uninstall:
	@echo "make $@: non ancora disponibile su Windows (arrivera' con i pacchetti; per ora: make dist)" >&2; exit 1

# cartella portabile build/dist/Ribalta (exe + DLL di MSYS2 + assets) e zip. Le DLL si trovano con
# ldd, tenendo solo quelle sotto il prefisso MSYS2 (es. /ucrt64): quelle di C:/Windows restano fuori.
DISTNAME    := Ribalta-windows-x86_64
DISTDIR     := build/dist/Ribalta
MSYS_PREFIX ?= $(if $(MSYSTEM_PREFIX),$(MSYSTEM_PREFIX),/ucrt64)
dist: release
	$(call say,DIST,$(DISTDIR))
	@rm -rf build/dist build/$(DISTNAME).zip
	@mkdir -p $(DISTDIR)/assets
	@cp $(BIN) $(DISTDIR)/
	@for f in font.ttf OFL.txt ding.wav ribalta.svg; do cp assets/$$f $(DISTDIR)/assets/; done
	@if [ -f LICENSE ]; then cp LICENSE $(DISTDIR)/; fi
	@ldd ./$(BIN) | awk '/=>/ {print $$3}' | grep -F "$(MSYS_PREFIX)/" | sort -u | while read -r dll; do cp "$$dll" $(DISTDIR)/; done
	@ls $(DISTDIR)/*.dll > /dev/null 2>&1 || { echo "make dist: nessuna DLL trovata sotto $(MSYS_PREFIX)" >&2; exit 1; }
	@cd build/dist && if command -v zip > /dev/null 2>&1; then zip -qr ../$(DISTNAME).zip Ribalta; \
	  else bsdtar -a -cf ../$(DISTNAME).zip Ribalta; fi
	$(call say,ZIP,build/$(DISTNAME).zip)
else
dist:
	@echo "make dist: per ora solo su Windows (MSYS2 UCRT64)" >&2; exit 1

# rimuove solo i file installati: configurazione e statistiche dell'utente restano
uninstall:
	@./tools/uninstall.sh "$(PREFIX)"

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

clean:
	@rm -rf build $(BIN)

-include $(OBJ:.o=.d)

# test unitari (senza display): ogni tests/test_*.c è un programma a sé, fallisce al primo errore
TESTS       := $(patsubst tests/%.c,%,$(wildcard tests/test_*.c))
TEST_COMMON := src/core/settings.c src/core/color.c src/core/timer.c src/platform/paths.c \
               src/platform/fonts_common.c $(FONTS_SRC) src/core/stats.c

build/tests/%$(EXE): tests/%.c $(TEST_COMMON)
	$(call say,CC,tests/$*.c)
	@mkdir -p build/tests
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(TEST_COMMON) $(LDLIBS)

# test_session: collega anche session.c, pomodoro.c e gli stub delle notifiche
build/tests/test_session$(EXE): tests/test_session.c tests/stub_notify.c src/app/session.c src/core/pomodoro.c $(TEST_COMMON)
	$(call say,CC,tests/test_session.c)
	@mkdir -p build/tests
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< tests/stub_notify.c src/app/session.c src/core/pomodoro.c $(TEST_COMMON) $(LDLIBS)

test: $(addprefix build/tests/,$(addsuffix $(EXE),$(TESTS)))
	@for t in $(TESTS); do printf '  %-4s %s ' TEST $$t; ./build/tests/$$t$(EXE) || exit 1; done

# rigenera le PNG dell'icona da assets/ribalta.svg (serve rsvg-convert; le PNG sono nel repo)
icons:
	./tools/icons.sh

.PHONY: all app debug release dist install install-files uninstall clean icons test
