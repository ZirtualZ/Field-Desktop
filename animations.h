#ifndef FIELD_DESKTOP_ANIMATIONS_H
#define FIELD_DESKTOP_ANIMATIONS_H

#include <X11/Xlib.h>

typedef struct {
    int enabled;
} DesktopAnimations;

void animations_init(DesktopAnimations *animations);
int animations_set_enabled(DesktopAnimations *animations, int enabled);
void animations_map_window(Display *display, Window window, int x, int y,
                           unsigned int width, unsigned int height,
                           const DesktopAnimations *animations);

#endif
