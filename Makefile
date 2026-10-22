CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
LDLIBS += -lX11 -lXrandr
PKG_CONFIG ?= pkg-config
GTK_CFLAGS = $(shell $(PKG_CONFIG) --cflags 'gtk4 >= 4.12' 2>/dev/null)
GTK_LIBS = $(shell $(PKG_CONFIG) --libs 'gtk4 >= 4.12' 2>/dev/null)
XFT_CFLAGS = $(shell pkg-config --cflags xft)
XFT_LIBS = $(shell pkg-config --libs xft)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
XSESSIONDIR ?= /usr/share/xsessions
DESTDIR ?=

.PHONY: all clean install check-gtk

all: simple-desktop simple-wm

DESKTOP_SOURCES = src/desktop.c src/applications.c src/desktop_items.c \
	src/appearance.c src/monitor_layout.c src/shell/session.c \
	src/shell/gtk_shell.c src/shell/desktop_view.c src/shell/launcher.c \
	src/shell/settings.c src/shell/icon_themes.c
DESKTOP_HEADERS = src/applications.h src/desktop_items.h src/appearance.h \
	src/monitor_layout.h src/shell/session.h src/shell/gtk_shell.h \
	src/shell/gtk_shell_internal.h src/shell/desktop_view.h \
	src/shell/launcher.h src/shell/settings.h src/shell/icon_themes.h

check-gtk:
	@command -v "$(PKG_CONFIG)" >/dev/null 2>&1 || { \
		echo "Error: pkg-config is required to build simple-desktop." >&2; \
		exit 1; \
	}
	@$(PKG_CONFIG) --exists 'gtk4 >= 4.12' || { \
		echo "Error: GTK 4.12+ development files and compatible GLib/GIO headers are required." >&2; \
		$(PKG_CONFIG) --cflags 'gtk4 >= 4.12' >&2; \
		exit 1; \
	}

simple-desktop: $(DESKTOP_SOURCES) $(DESKTOP_HEADERS) | check-gtk
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -std=c11 -o $@ $(DESKTOP_SOURCES) \
		$(LDLIBS) $(GTK_LIBS)

simple-wm: src/wm.c src/monitor_layout.c src/monitor_layout.h
	$(CC) $(CFLAGS) $(XFT_CFLAGS) -std=c11 -o $@ \
		src/wm.c src/monitor_layout.c $(LDLIBS) $(XFT_LIBS)

install: all
	install -Dm755 simple-desktop "$(DESTDIR)$(BINDIR)/simple-desktop"
	install -Dm755 simple-wm "$(DESTDIR)$(BINDIR)/simple-wm"
	install -Dm644 data/field-desktop.desktop \
		"$(DESTDIR)$(XSESSIONDIR)/field-desktop.desktop"

clean:
	rm -f simple-desktop simple-wm