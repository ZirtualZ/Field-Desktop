CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
LDLIBS += -lX11 -lXrandr
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
XSESSIONDIR ?= /usr/share/xsessions
DESTDIR ?=

.PHONY: all clean install

all: simple-desktop simple-wm

DESKTOP_SOURCES = src/desktop.c src/applications.c src/desktop_items.c \
	src/appearance.c src/startup_apps.c src/animations.c
DESKTOP_HEADERS = src/applications.h src/desktop_items.h src/appearance.h \
	src/startup_apps.h src/animations.h

simple-desktop: $(DESKTOP_SOURCES) $(DESKTOP_HEADERS)
	$(CC) $(CFLAGS) -std=c11 -o $@ $(DESKTOP_SOURCES) $(LDLIBS)

simple-wm: src/wm.c
	$(CC) $(CFLAGS) -std=c11 -o $@ $< $(LDLIBS)

install: all
	install -Dm755 simple-desktop "$(DESTDIR)$(BINDIR)/simple-desktop"
	install -Dm755 simple-wm "$(DESTDIR)$(BINDIR)/simple-wm"
	install -Dm644 data/field-desktop.desktop \
		"$(DESTDIR)$(XSESSIONDIR)/field-desktop.desktop"

clean:
	rm -f simple-desktop simple-wm