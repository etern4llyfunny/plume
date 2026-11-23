CFLAGS = -O2 -Wall -Wextra
LIBS   = $(shell pkg-config --cflags --libs gtk4 gtk4-layer-shell-0)

all: plume-launcher plume-power

plume-launcher: plume-launcher.c
	gcc $(CFLAGS) $< -o $@ $(LIBS)

plume-power: plume-power.c
	gcc $(CFLAGS) $< -o $@ $(LIBS)

clean:
	rm -f plume-launcher plume-power
