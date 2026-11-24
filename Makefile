CFLAGS = -O2 -Wall -Wextra
LIBS   = $(shell pkg-config --cflags --libs gtk4 gtk4-layer-shell-0)
GTK4   = $(shell pkg-config --cflags --libs gtk4)

all: plume-launcher plume-power plume-settings

plume-launcher: plume-launcher.c
	gcc $(CFLAGS) $< -o $@ $(LIBS)

plume-power: plume-power.c
	gcc $(CFLAGS) $< -o $@ $(LIBS)

plume-settings: plume-settings.c
	gcc $(CFLAGS) $< -o $@ $(GTK4)

clean:
	rm -f plume-launcher plume-power plume-settings
