CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
LDLIBS += -lX11 -lXrandr
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
XSESSIONDIR ?= /usr/share/xsessions
DESTDIR ?=

.PHONY: all clean install

all: simple-desktop simple-wm

DESKTOP_SOURCES = desktop.c applications.c desktop_items.c appearance.c \
	startup_apps.c animations.c
DESKTOP_HEADERS = applications.h desktop_items.h appearance.h \
	startup_apps.h animations.h

simple-desktop: $(DESKTOP_SOURCES) $(DESKTOP_HEADERS)
	$(CC) $(CFLAGS) -std=c11 -o $@ $(DESKTOP_SOURCES) $(LDLIBS)

simple-wm: wm.c
	$(CC) $(CFLAGS) -std=c11 -o $@ $< $(LDLIBS)

install: all
	install -Dm755 simple-desktop "$(DESTDIR)$(BINDIR)/simple-desktop"
	install -Dm755 simple-wm "$(DESTDIR)$(BINDIR)/simple-wm"
	install -Dm644 data/field-desktop.desktop \
		"$(DESTDIR)$(XSESSIONDIR)/field-desktop.desktop"

clean:
	rm -f simple-desktop simple-wm