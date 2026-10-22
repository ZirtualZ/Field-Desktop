/* Owns drawing of per-monitor backgrounds, panels, shortcuts, and files. */
#include "desktop_view.h"

#include "../icon_theme.h"

#include <stdio.h>
#include <string.h>

enum {
    DESKTOP_ITEM_ROWS = 6
};

int desktop_quick_actions_enabled(const int quick_actions[6])
{
    for (int i = 0; i < 6; ++i) {
        if (quick_actions[i]) {
            return 1;
        }
    }
    return 0;
}

int desktop_quick_action_columns(int monitor_width)
{
    return monitor_width >= 620 ? 6 : (monitor_width >= 330 ? 3 : 2);
}

int desktop_quick_action_count(const int quick_actions[6])
{
    int count = 0;
    for (int i = 0; i < 6; ++i) {
        count += quick_actions[i] != 0;
    }
    return count;
}

int desktop_quick_action_position(const int quick_actions[6], int action,
                                  int monitor_width, int *x, int *y)
{
    if (action < 0 || action >= 6 || !quick_actions[action]) {
        return 0;
    }
    int position = 0;
    for (int i = 0; i < action; ++i) {
        position += quick_actions[i] != 0;
    }
    int columns = desktop_quick_action_columns(monitor_width);
    *x = 28 + (position % columns) * 98;
    *y = 84 + (position / columns) * 96;
    return 1;
}

int desktop_item_visible_rows(int height, int start_y)
{
    int rows = (height - start_y - 80) / 94 + 1;
    if (rows > DESKTOP_ITEM_ROWS) {
        rows = DESKTOP_ITEM_ROWS;
    }
    return rows < 1 ? 1 : rows;
}

static void draw_builtin_shortcut_icon(const DesktopView *view,
                                       int action, int x, int y)
{
    Display *display = view->display;
    Window window = view->window;
    GC gc = view->gc;
    XSetForeground(display, gc, view->accent);
    if (action == 0) {
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 2; ++column) {
                XFillRectangle(display, window, gc, x + 15 + column * 17,
                               y + 14 + row * 17, 12, 12);
            }
        }
    } else if (action == 1) {
        XFillRectangle(display, window, gc, x + 12, y + 11, 38, 30);
        XSetForeground(display, gc, view->panel);
        XDrawString(display, window, gc, x + 19, y + 31, ">_", 2);
    } else if (action == 2) {
        XFillRectangle(display, window, gc, x + 10, y + 17, 20, 8);
        XFillRectangle(display, window, gc, x + 10, y + 22, 42, 23);
        XSetForeground(display, gc, view->foreground);
        XDrawLine(display, window, gc, x + 15, y + 28, x + 47, y + 28);
    } else if (action == 3) {
        XFillArc(display, window, gc, x + 11, y + 10, 40, 40, 0, 360 * 64);
        XSetForeground(display, gc, view->panel);
        XDrawArc(display, window, gc, x + 20, y + 10, 22, 40, 0, 360 * 64);
        XDrawLine(display, window, gc, x + 12, y + 30, x + 50, y + 30);
    } else if (action == 4) {
        XDrawArc(display, window, gc, x + 13, y + 12, 36, 36, 0, 360 * 64);
        XFillArc(display, window, gc, x + 25, y + 24, 12, 12, 0, 360 * 64);
        static const int starts[][2] = {
            {31, 6}, {48, 13}, {55, 30}, {48, 47},
            {31, 54}, {14, 47}, {7, 30}, {14, 13}
        };
        static const int ends[][2] = {
            {31, 12}, {43, 18}, {49, 30}, {43, 42},
            {31, 48}, {19, 42}, {13, 30}, {19, 18}
        };
        for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
            XDrawLine(display, window, gc, x + starts[i][0], y + starts[i][1],
                      x + ends[i][0], y + ends[i][1]);
        }
    } else {
        XFillArc(display, window, gc, x + 14, y + 11, 34, 34, 0, 360 * 64);
        XSetForeground(display, gc, view->panel);
        XDrawString(display, window, gc, x + 26, y + 35, "?", 1);
    }
}

static void draw_monitor_background(const DesktopView *view,
                                    MonitorGeometry monitor)
{
    Display *display = view->display;
    GC gc = view->gc;
    Window window = view->window;
    unsigned int wallpaper_width = 0;
    unsigned int wallpaper_height = 0;
    Pixmap wallpaper = view->appearance->image_enabled &&
                       view->wallpaper_applied
        ? appearance_wallpaper_pixmap(display, &wallpaper_width,
                                      &wallpaper_height) : None;
    if (wallpaper != None && monitor.x >= 0 && monitor.y >= 0 &&
        (unsigned int)monitor.x < wallpaper_width &&
        (unsigned int)monitor.y < wallpaper_height) {
        unsigned int copy_width = wallpaper_width - (unsigned int)monitor.x;
        unsigned int copy_height = wallpaper_height - (unsigned int)monitor.y;
        if (copy_width > (unsigned int)monitor.width) {
            copy_width = (unsigned int)monitor.width;
        }
        if (copy_height > (unsigned int)monitor.height) {
            copy_height = (unsigned int)monitor.height;
        }
        XCopyArea(display, wallpaper, window, gc, monitor.x, monitor.y,
                  copy_width, copy_height, monitor.x, monitor.y);
        return;
    }
    XSetForeground(display, gc, view->glow);
    XFillArc(display, window, gc, monitor.x + monitor.width - 350,
             monitor.y + monitor.height - 320, 420, 420, 0, 360 * 64);
    XSetForeground(display, gc, view->background);
    XFillArc(display, window, gc, monitor.x + monitor.width - 290,
             monitor.y + monitor.height - 265, 340, 340, 0, 360 * 64);
}

static void draw_top_panel(const DesktopView *view, MonitorGeometry monitor,
                           size_t monitor_index)
{
    Display *display = view->display;
    Window window = view->window;
    GC gc = view->gc;
    if (!view->top_panel) {
        return;
    }
    XSetForeground(display, gc, view->panel);
    XFillRectangle(display, window, gc, monitor.x, monitor.y,
                   (unsigned int)monitor.width, 42);
    XSetForeground(display, gc, view->accent);
    XFillRectangle(display, window, gc, monitor.x + 18, monitor.y + 12,
                   18, 18);
    XSetForeground(display, gc, view->foreground);
    XDrawString(display, window, gc, monitor.x + 48, monitor.y + 27,
                "FIELD DESKTOP", 13);
    char display_name[32];
    snprintf(display_name, sizeof(display_name), "DISPLAY %zu",
             monitor_index + 1);
    XSetForeground(display, gc, view->glow);
    XDrawString(display, window, gc, monitor.x + monitor.width - 110,
                monitor.y + 27, display_name, (int)strlen(display_name));
}

static void draw_shortcuts(const DesktopView *view, MonitorGeometry monitor)
{
    static const char *const labels[] = {
        "Apps", "Terminal", "Files", "Browser", "Settings", "Help"
    };
    if (!desktop_quick_actions_enabled(view->quick_actions)) {
        return;
    }
    for (int action = 0; action < 6; ++action) {
        int local_x;
        int local_y;
        if (!desktop_quick_action_position(view->quick_actions, action,
                                           monitor.width, &local_x, &local_y)) {
            continue;
        }
        int x = monitor.x + local_x;
        int y = monitor.y + local_y;
        XSetForeground(view->display, view->gc, view->panel);
        XFillRectangle(view->display, view->window, view->gc, x, y, 70, 82);
        XSetForeground(view->display, view->gc, view->accent);
        XDrawRectangle(view->display, view->window, view->gc, x, y, 69, 81);
        if (!icon_theme_draw(view->display, view->window, view->gc, action,
                             x + 11, y + 6, view->panel)) {
            draw_builtin_shortcut_icon(view, action, x + 10, y + 5);
        }
        XSetForeground(view->display, view->gc, view->foreground);
        int text_width = (int)strlen(labels[action]) * 6;
        XDrawString(view->display, view->window, view->gc,
                    x + (70 - text_width) / 2, y + 72,
                    labels[action], (int)strlen(labels[action]));
    }
}

static int desktop_items_start_y(const DesktopView *view, int monitor_width)
{
    int count = desktop_quick_action_count(view->quick_actions);
    int columns = desktop_quick_action_columns(monitor_width);
    int rows = (count + columns - 1) / columns;
    return count > 0 ? 84 + rows * 96 + 20 : 84;
}

static void draw_desktop_item(const DesktopView *view, const DesktopItem *item,
                              int x, int y, size_t index)
{
    if ((int)index == view->selected_item) {
        XSetForeground(view->display, view->gc, view->panel);
        XFillRectangle(view->display, view->window, view->gc, x, y, 104, 82);
    }
    int themed = icon_theme_draw(
        view->display, view->window, view->gc,
        item->is_directory ? DESKTOP_ICON_FOLDER : DESKTOP_ICON_FILE,
        x + 28, y + 2, view->background);
    XSetForeground(view->display, view->gc, view->accent);
    if (!themed && item->is_directory) {
        XFillRectangle(view->display, view->window, view->gc, x + 31, y + 13,
                       40, 24);
        XFillRectangle(view->display, view->window, view->gc, x + 26, y + 20,
                       50, 29);
    } else if (!themed) {
        XFillRectangle(view->display, view->window, view->gc, x + 35, y + 8,
                       32, 42);
        XSetForeground(view->display, view->gc, view->panel);
        XDrawLine(view->display, view->window, view->gc, x + 41, y + 22,
                  x + 61, y + 22);
        XDrawLine(view->display, view->window, view->gc, x + 41, y + 29,
                  x + 61, y + 29);
        XDrawLine(view->display, view->window, view->gc, x + 41, y + 36,
                  x + 57, y + 36);
    }
    XSetForeground(view->display, view->gc, view->foreground);
    int name_length = (int)strlen(item->name);
    XDrawString(view->display, view->window, view->gc, x + 4, y + 69,
                item->name, name_length > 12 ? 12 : name_length);
}

static void draw_desktop_items(const DesktopView *view, MonitorGeometry monitor)
{
    if (!view->desktop_files || view->items == NULL ||
        view->items->count == 0) {
        return;
    }
    int start_y = desktop_items_start_y(view, monitor.width);
    int columns = monitor.width > 48 ? (monitor.width - 48) / 116 : 1;
    if (columns > DESKTOP_ITEM_COLUMNS) {
        columns = DESKTOP_ITEM_COLUMNS;
    }
    if (columns < 1) {
        columns = 1;
    }
    size_t visible = (size_t)desktop_item_visible_rows(monitor.height, start_y) *
                     (size_t)columns;
    if (visible > view->items->count) {
        visible = view->items->count;
    }
    for (size_t i = 0; i < visible; ++i) {
        const DesktopItem *item = desktop_items_get(view->items, i);
        int x = monitor.x + 24 + (int)(i % (size_t)columns) * 116;
        int y = monitor.y + start_y + (int)(i / (size_t)columns) * 94;
        draw_desktop_item(view, item, x, y, i);
    }
    if (visible < view->items->count) {
        char overflow[64];
        int length = snprintf(overflow, sizeof(overflow),
                              "+%zu more in Desktop folder",
                              view->items->count - visible);
        XSetForeground(view->display, view->gc, view->foreground);
        XDrawString(view->display, view->window, view->gc, monitor.x + 24,
                    monitor.y + monitor.height - 48, overflow, length);
    }
}

void desktop_view_draw(const DesktopView *view)
{
    XSetForeground(view->display, view->gc, view->background);
    XFillRectangle(view->display, view->window, view->gc, 0, 0,
                   (unsigned int)view->width, (unsigned int)view->height);
    for (size_t i = 0; i < view->monitor_count; ++i) {
        MonitorGeometry monitor = view->monitors[i];
        draw_monitor_background(view, monitor);
        draw_top_panel(view, monitor, i);
        draw_shortcuts(view, monitor);
        draw_desktop_items(view, monitor);
    }
}
