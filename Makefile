CC      ?= cc
PKGS    := sdl3 sdl3-ttf
CFLAGS  ?= -std=c17 -Wall -Wextra -Wpedantic -O2
override CFLAGS += $(shell pkg-config --cflags $(PKGS)) -D_POSIX_C_SOURCE=200809L
override LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=build/%.o)
BIN     := ribalta
PREFIX  ?= $(HOME)/.local
ICONS   := $(PREFIX)/share/icons/hicolor
ICON_SIZES := 16 22 24 32 48 64 128 256 512

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
	install -Dm644 ribalta.desktop $(PREFIX)/share/applications/ribalta.desktop
	install -Dm644 -t $(PREFIX)/share/ribalta assets/font.ttf assets/OFL.txt assets/ding.wav assets/ribalta.svg
	install -Dm644 assets/ribalta.svg $(ICONS)/scalable/apps/ribalta.svg
	for n in $(ICON_SIZES); do \
	    install -Dm644 assets/icons/$${n}x$${n}.png $(ICONS)/$${n}x$${n}/apps/ribalta.png; \
	done
	# aggiorna la cache delle icone solo se esiste già (una cache vecchia nasconde le icone nuove)
	if [ -f $(ICONS)/icon-theme.cache ] && command -v gtk-update-icon-cache >/dev/null; then \
	    gtk-update-icon-cache -q -f -t $(ICONS) || true; \
	fi

uninstall:
	rm -rf $(PREFIX)/bin/$(BIN) $(PREFIX)/share/ribalta $(PREFIX)/share/applications/ribalta.desktop \
	      $(ICONS)/scalable/apps/ribalta.svg
	for n in $(ICON_SIZES); do rm -f $(ICONS)/$${n}x$${n}/apps/ribalta.png; done

clean:
	rm -rf build $(BIN)

-include $(OBJ:.o=.d)
# rigenera le PNG dell'icona da assets/ribalta.svg (serve rsvg-convert; le PNG sono nel repo)
icons:
	./tools/icons.sh

.PHONY: all debug release install uninstall clean icons
