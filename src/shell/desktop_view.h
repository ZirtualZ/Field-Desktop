/* Owns desktop background, panel, shortcuts, and Desktop-folder rendering. */
#ifndef FIELD_DESKTOP_SHELL_DESKTOP_VIEW_H
#define FIELD_DESKTOP_SHELL_DESKTOP_VIEW_H

#include "../appearance.h"
#include "../desktop_items.h"
#include "../monitor_layout.h"

#include <X11/Xlib.h>

#include <stddef.h>

enum {
    DESKTOP_ITEM_COLUMNS = 9
};

typedef struct {
    Display *display;
    Window window;
    GC gc;
    int width;
    int height;
    unsigned long background;
    unsigned long foreground;
    unsigned long accent;
    unsigned long panel;
    unsigned long glow;
    const Appearance *appearance;
    int wallpaper_applied;
    const DesktopItems *items;
    int selected_item;
    const MonitorGeometry *monitors;
    size_t monitor_count;
    const int *quick_actions;
    int top_panel;
    int desktop_files;
} DesktopView;

int desktop_quick_actions_enabled(const int quick_actions[6]);
int desktop_quick_action_columns(int monitor_width);
int desktop_quick_action_count(const int quick_actions[6]);
int desktop_quick_action_position(const int quick_actions[6], int action,
                                  int monitor_width, int *x, int *y);
int desktop_item_visible_rows(int height, int start_y);
void desktop_view_draw(const DesktopView *view);

#endif
