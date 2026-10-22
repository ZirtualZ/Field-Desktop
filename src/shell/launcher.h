/* Owns the application launcher window and its filtered result rendering. */
#ifndef FIELD_DESKTOP_SHELL_LAUNCHER_H
#define FIELD_DESKTOP_SHELL_LAUNCHER_H

#include "../animations.h"
#include "../applications.h"
#include "../monitor_layout.h"

#include <X11/Xlib.h>

#include <stddef.h>

enum {
    LAUNCHER_WIDTH = 520,
    LAUNCHER_HEIGHT = 430,
    LAUNCHER_ROWS = 10
};

int launcher_matching_count(const Application *applications, size_t count,
                            const char *query);
int launcher_matching_at(const Application *applications, size_t count,
                         const char *query, int position);
void launcher_draw(Display *display, Window window, GC gc,
                   unsigned long foreground, unsigned long accent,
                   unsigned long panel, unsigned long subdued,
                   const Application *applications, size_t count,
                   const char *query, int selected);
Window launcher_open(Display *display, Window root, Window existing, GC gc,
                     unsigned long foreground, unsigned long accent,
                     unsigned long panel, unsigned long subdued,
                     const MonitorGeometry *primary_monitor,
                     const Application *applications, size_t count,
                     const char *query, int selected,
                     const DesktopAnimations *animation_settings);

#endif
