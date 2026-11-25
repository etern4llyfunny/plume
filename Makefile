PREFIX ?= /usr
CFLAGS = -O2 -Wall -Wextra
LIBS   = $(shell pkg-config --cflags --libs gtk4 gtk4-layer-shell-0 wayland-client gio-unix-2.0)
GTK4   = $(shell pkg-config --cflags --libs gtk4)

BINS    = plume-launcher plume-power plume-settings plume-dock
SCRIPTS = plume-session plume-apply-theme plume-lock plume-screenshot plume-pinned plume-ws

all: $(BINS)

.PHONY: all install clean

plume-launcher: plume-launcher.c
	gcc $(CFLAGS) $< -o $@ $(LIBS)

plume-power: plume-power.c
	gcc $(CFLAGS) $< -o $@ $(LIBS)

plume-settings: plume-settings.c
	gcc $(CFLAGS) $< -o $@ $(GTK4)

plume-dock: plume-dock.c wlr-foreign-toplevel-unstable-v1-protocol.c
	gcc $(CFLAGS) plume-dock.c wlr-foreign-toplevel-unstable-v1-protocol.c -o $@ $(LIBS)

install: all
	install -dm755 $(DESTDIR)$(PREFIX)/bin
	install -m755 $(BINS) $(SCRIPTS) $(DESTDIR)$(PREFIX)/bin/
	install -dm755 $(DESTDIR)$(PREFIX)/share/plume
	cp -r assets config $(DESTDIR)$(PREFIX)/share/plume/
	install -dm755 $(DESTDIR)$(PREFIX)/share/themes
	cp -r share/themes/* $(DESTDIR)$(PREFIX)/share/themes/
	install -Dm644 plume.desktop $(DESTDIR)$(PREFIX)/share/wayland-sessions/plume.desktop
	install -Dm644 share/applications/plume-settings.desktop $(DESTDIR)$(PREFIX)/share/applications/plume-settings.desktop

clean:
	rm -f $(BINS)
