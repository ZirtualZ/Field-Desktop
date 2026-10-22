#ifndef FIELD_DESKTOP_ICON_THEME_H
#define FIELD_DESKTOP_ICON_THEME_H

#include <X11/Xlib.h>

#include <stddef.h>

enum {
    DESKTOP_ICON_APPS,
    DESKTOP_ICON_TERMINAL,
    DESKTOP_ICON_FILES,
    DESKTOP_ICON_BROWSER,
    DESKTOP_ICON_SETTINGS,
    DESKTOP_ICON_HELP,
    DESKTOP_ICON_FOLDER,
    DESKTOP_ICON_FILE,
    DESKTOP_ICON_COUNT
};

void icon_theme_initialize(Display *display);
size_t icon_theme_count(void);
const char *icon_theme_name(size_t index);
size_t icon_theme_selected(void);
int icon_theme_select(Display *display, size_t index);
int icon_theme_draw(Display *display, Drawable drawable, GC gc, int icon,
                    int x, int y, unsigned long background);
void icon_theme_clear(Display *display);

#endif
