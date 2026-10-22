CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
LDLIBS += -lX11 -lXrandr
GTK_CFLAGS = $(shell pkg-config --cflags 'gtk4 >= 4.12')
GTK_LIBS = $(shell pkg-config --libs 'gtk4 >= 4.12')
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
XSESSIONDIR ?= /usr/share/xsessions
DESTDIR ?=

.PHONY: all clean install

all: simple-desktop simple-wm

DESKTOP_SOURCES = src/desktop.c src/applications.c src/desktop_items.c \
	src/appearance.c src/monitor_layout.c src/shell/session.c \
	src/shell/gtk_shell.c src/shell/desktop_view.c src/shell/launcher.c \
	src/shell/settings.c
DESKTOP_HEADERS = src/applications.h src/desktop_items.h src/appearance.h \
	src/monitor_layout.h src/shell/session.h src/shell/gtk_shell.h \
	src/shell/gtk_shell_internal.h src/shell/desktop_view.h \
	src/shell/launcher.h src/shell/settings.h

simple-desktop: $(DESKTOP_SOURCES) $(DESKTOP_HEADERS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -std=c11 -o $@ $(DESKTOP_SOURCES) \
		$(LDLIBS) $(GTK_LIBS)

simple-wm: src/wm.c src/monitor_layout.c src/monitor_layout.h
	$(CC) $(CFLAGS) -std=c11 -o $@ src/wm.c src/monitor_layout.c $(LDLIBS)

install: all
	install -Dm755 simple-desktop "$(DESTDIR)$(BINDIR)/simple-desktop"
	install -Dm755 simple-wm "$(DESTDIR)$(BINDIR)/simple-wm"
	install -Dm644 data/field-desktop.desktop \
		"$(DESTDIR)$(XSESSIONDIR)/field-desktop.desktop"

clean:
	rm -f simple-desktop simple-wm