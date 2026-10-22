/* Owns launcher filtering, drawing, and placement on the primary monitor. */
#include "launcher.h"

#include <X11/Xutil.h>

#include <stdio.h>
#include <string.h>

int launcher_matching_count(const Application *applications, size_t count,
                            const char *query)
{
    int matches = 0;
    for (size_t i = 0; i < count; ++i) {
        if (application_matches(&applications[i], query)) {
            ++matches;
        }
    }
    return matches;
}

int launcher_matching_at(const Application *applications, size_t count,
                         const char *query, int position)
{
    for (size_t i = 0; i < count; ++i) {
        if (application_matches(&applications[i], query) && position-- == 0) {
            return (int)i;
        }
    }
    return -1;
}

void launcher_draw(Display *display, Window window, GC gc,
                   unsigned long foreground, unsigned long accent,
                   unsigned long panel, unsigned long subdued,
                   const Application *applications, size_t count,
                   const char *query, int selected)
{
    XSetForeground(display, gc, panel);
    XFillRectangle(display, window, gc, 0, 0,
                   LAUNCHER_WIDTH, LAUNCHER_HEIGHT);
    XSetForeground(display, gc, accent);
    XFillRectangle(display, window, gc, 0, 0, 5, LAUNCHER_HEIGHT);
    XSetForeground(display, gc, foreground);
    XDrawString(display, window, gc, 28, 34, "APPLICATIONS", 12);
    XSetForeground(display, gc, subdued);
    const char *help = "Type to search  |  Enter to launch  |  Esc to close";
    XDrawString(display, window, gc, 28, 59, help, (int)strlen(help));

    XSetForeground(display, gc, panel);
    XFillRectangle(display, window, gc, 24, 76, LAUNCHER_WIDTH - 48, 36);
    XSetForeground(display, gc, foreground);
    char search_line[sizeof("Search: ") + 128];
    snprintf(search_line, sizeof(search_line), "Search: %s", query);
    XDrawString(display, window, gc, 34, 99, search_line,
                (int)strlen(search_line));

    int result_count = launcher_matching_count(applications, count, query);
    int first = selected >= LAUNCHER_ROWS ? selected - LAUNCHER_ROWS + 1 : 0;
    int shown = 0;
    for (int position = first; position < result_count &&
         shown < LAUNCHER_ROWS; ++position, ++shown) {
        int application_index = launcher_matching_at(
            applications, count, query, position);
        if (application_index < 0) {
            break;
        }
        int y = 124 + shown * 27;
        if (position == selected) {
            XSetForeground(display, gc, accent);
            XFillRectangle(display, window, gc, 24, y - 17,
                           LAUNCHER_WIDTH - 48, 24);
        }
        const char *name = applications[application_index].name;
        XSetForeground(display, gc, position == selected ? panel : foreground);
        XDrawString(display, window, gc, 36, y, name,
                    (int)strlen(name) > 58 ? 58 : (int)strlen(name));
    }
    if (result_count == 0) {
        XSetForeground(display, gc, subdued);
        XDrawString(display, window, gc, 36, 145,
                    "No matching installed applications", 34);
    }
}

Window launcher_open(Display *display, Window root, Window existing, GC gc,
                     unsigned long foreground, unsigned long accent,
                     unsigned long panel, unsigned long subdued,
                     const MonitorGeometry *primary_monitor,
                     const Application *applications, size_t count,
                     const char *query, int selected,
                     const DesktopAnimations *animation_settings)
{
    if (existing != None) {
        XRaiseWindow(display, existing);
        XSetInputFocus(display, existing, RevertToParent, CurrentTime);
        return existing;
    }

    int x = primary_monitor->x +
            (primary_monitor->width - LAUNCHER_WIDTH) / 2;
    int y = primary_monitor->y +
            (primary_monitor->height - LAUNCHER_HEIGHT) / 2;
    XSetWindowAttributes attributes;
    attributes.override_redirect = True;
    attributes.background_pixel = panel;
    attributes.event_mask = ExposureMask | ButtonPressMask | KeyPressMask;
    Window launcher = XCreateWindow(
        display, root, x, y, LAUNCHER_WIDTH, LAUNCHER_HEIGHT, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
    animations_map_window(display, launcher, x, y, LAUNCHER_WIDTH,
                          LAUNCHER_HEIGHT, animation_settings);
    XSetInputFocus(display, launcher, RevertToParent, CurrentTime);
    launcher_draw(display, launcher, gc, foreground, accent, panel, subdued,
                  applications, count, query, selected);
    XFlush(display);
    return launcher;
}
