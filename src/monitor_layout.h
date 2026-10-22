#ifndef FIELD_DESKTOP_MONITOR_LAYOUT_H
#define FIELD_DESKTOP_MONITOR_LAYOUT_H

#include <X11/Xlib.h>

#include <stddef.h>
#include <stdio.h>

typedef struct {
    int x;
    int y;
    int width;
    int height;
} MonitorGeometry;

size_t monitor_layout_get(Display *display, Window root,
                          MonitorGeometry *monitors, size_t capacity);
long monitor_layout_event_mask(Display *display);
int monitor_layout_set_primary(Display *display, Window root, size_t index);

#endif
