#define _POSIX_C_SOURCE 200809L

#include "monitor_layout.h"

#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/extensions/Xrandr.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    MAX_CLIENTS = 128,
    TITLE_HEIGHT = 38,
    MIN_WIDTH = 240,
    MIN_HEIGHT = 140,
    RESIZE_BORDER = 8,
    SWITCHER_ROWS = 10,
    SWITCHER_WIDTH = 480,
    SWITCHER_ROW_HEIGHT = 42,
    TASKBAR_HEIGHT = 48,
    TASKBAR_BUTTON_MIN_WIDTH = 112,
    TASKBAR_BUTTON_MAX_WIDTH = 280,
    MAX_MONITORS = 16
};

typedef struct {
    Window client;
    Window frame;
    int x;
    int y;
    int width;
    int height;
} ManagedWindow;

static Display *display;
static Window root;
static ManagedWindow clients[MAX_CLIENTS];
static int client_count;
static int focused = -1;
static int next_position;
static unsigned long frame_color;
static unsigned long active_color;
static unsigned long switcher_color;
static unsigned long panel_color;
static unsigned long button_color;
static unsigned long button_active_color;
static unsigned long outline_color;
static unsigned long close_color;
static GC gc;
static XftFont *ui_font;
static XFontStruct *core_font;
static XftColor text_xft;
static XftColor muted_xft;
static XftColor accent_xft;
static XftColor dark_xft;
static int startup_failed;
static Window switcher_window;
static Window taskbar_windows[MAX_MONITORS];
static MonitorGeometry monitor_layout[MAX_MONITORS];
static size_t monitor_count = 1;
static int taskbar_enabled = 1;
static int randr_event_base;
static int randr_error_base;

static void show_switcher(void);
static void draw_taskbar(void);
static void sync_taskbars(void);

static unsigned long named_color(const char *name)
{
    XColor exact;
    XColor allocated;
    if (!XAllocNamedColor(display, DefaultColormap(display, DefaultScreen(display)),
                          name, &allocated, &exact)) {
        return BlackPixel(display, DefaultScreen(display));
    }
    return allocated.pixel;
}

static void draw_round_rect(Drawable drawable, int x, int y, int width,
                            int height, int radius, unsigned long color)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    if (radius * 2 > width) {
        radius = width / 2;
    }
    if (radius * 2 > height) {
        radius = height / 2;
    }
    XSetForeground(display, gc, color);
    XFillRectangle(display, drawable, gc, x + radius, y,
                   (unsigned int)(width - radius * 2),
                   (unsigned int)height);
    XFillRectangle(display, drawable, gc, x, y + radius,
                   (unsigned int)width,
                   (unsigned int)(height - radius * 2));
    XFillArc(display, drawable, gc, x, y, (unsigned int)(radius * 2),
             (unsigned int)(radius * 2), 0, 360 * 64);
    XFillArc(display, drawable, gc, x + width - radius * 2, y,
             (unsigned int)(radius * 2), (unsigned int)(radius * 2),
             0, 360 * 64);
    XFillArc(display, drawable, gc, x, y + height - radius * 2,
             (unsigned int)(radius * 2), (unsigned int)(radius * 2),
             0, 360 * 64);
    XFillArc(display, drawable, gc, x + width - radius * 2,
             y + height - radius * 2, (unsigned int)(radius * 2),
             (unsigned int)(radius * 2), 0, 360 * 64);
}

static void draw_text(Drawable drawable, const char *text, int x, int baseline,
                      const XftColor *color, int max_width)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    char clipped[512];
    size_t length = strlen(text);
    if (length >= sizeof(clipped)) {
        length = sizeof(clipped) - 1;
    }
    memcpy(clipped, text, length);
    clipped[length] = '\0';
    if (ui_font == NULL) {
        if (core_font != NULL) {
            XSetForeground(display, gc, color->pixel);
            XDrawString(display, drawable, gc, x, baseline, clipped,
                        (int)length);
        }
        return;
    }
    XGlyphInfo extents;
    XftTextExtentsUtf8(display, ui_font, (const FcChar8 *)clipped,
                       (int)length, &extents);
    while (length > 1 && extents.xOff > max_width) {
        --length;
        while (length > 0 &&
               (((unsigned char)clipped[length] & 0xc0U) == 0x80U)) {
            --length;
        }
        clipped[length] = '\0';
        XftTextExtentsUtf8(display, ui_font, (const FcChar8 *)clipped,
                           (int)length, &extents);
    }
    XftDraw *draw = XftDrawCreate(display, drawable,
                                  DefaultVisual(display, DefaultScreen(display)),
                                  DefaultColormap(display, DefaultScreen(display)));
    if (draw != NULL) {
        XftDrawStringUtf8(draw, color, ui_font, x, baseline,
                          (const FcChar8 *)clipped, (int)length);
        XftDrawDestroy(draw);
    }
}

static void allocate_text_color(const char *name, XftColor *color)
{
    XColor exact;
    XColor allocated;
    if (!XAllocNamedColor(display,
                          DefaultColormap(display, DefaultScreen(display)),
                          name, &allocated, &exact)) {
        memset(color, 0, sizeof(*color));
        color->color.red = 0xffff;
        color->color.green = 0xffff;
        color->color.blue = 0xffff;
        color->color.alpha = 0xffff;
        return;
    }
    color->pixel = allocated.pixel;
    color->color.red = allocated.red;
    color->color.green = allocated.green;
    color->color.blue = allocated.blue;
    color->color.alpha = 0xffff;
}

static void taskbar_layout(int width, int *button_width, int *shown,
                           int *offset)
{
    int available = width - 24;
    *button_width = client_count > 0 ? available / client_count : available;
    if (*button_width < TASKBAR_BUTTON_MIN_WIDTH) {
        *button_width = TASKBAR_BUTTON_MIN_WIDTH;
    }
    if (*button_width > TASKBAR_BUTTON_MAX_WIDTH) {
        *button_width = TASKBAR_BUTTON_MAX_WIDTH;
    }
    *shown = available / *button_width;
    if (*shown > client_count) {
        *shown = client_count;
    }
    *offset = 12;
}

static int find_client(Window window)
{
    for (int i = 0; i < client_count; ++i) {
        if (clients[i].client == window || clients[i].frame == window) {
            return i;
        }
    }
    return -1;
}

static int is_desktop_surface(Window window)
{
    Atom window_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False);
    Atom desktop_type = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP",
                                    False);
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *value = NULL;
    int is_desktop = 0;
    if (XGetWindowProperty(display, window, window_type, 0, 16, False, XA_ATOM,
                           &actual_type, &actual_format, &item_count,
                           &bytes_after, &value) == Success &&
        value != NULL && actual_type == XA_ATOM && actual_format == 32) {
        Atom *types = (Atom *)value;
        for (unsigned long i = 0; i < item_count; ++i) {
            if (types[i] == desktop_type) {
                is_desktop = 1;
                break;
            }
        }
    }
    if (value != NULL) {
        XFree(value);
    }
    return is_desktop;
}

static void draw_frame(int index)
{
    ManagedWindow *managed = &clients[index];
    XSetForeground(display, gc, frame_color);
    XFillRectangle(display, managed->frame, gc, 0, 0,
                   (unsigned int)(managed->width + RESIZE_BORDER),
                   (unsigned int)(managed->height + TITLE_HEIGHT +
                                  RESIZE_BORDER));
    XSetForeground(display, gc,
                   index == focused ? active_color : panel_color);
    XFillRectangle(display, managed->frame, gc, 0, 0,
                   (unsigned int)(managed->width + RESIZE_BORDER),
                   TITLE_HEIGHT);

    char *name = NULL;
    if (!XFetchName(display, managed->client, &name) || name == NULL) {
        name = strdup("Application");
    }
    if (name != NULL) {
        XSetForeground(display, gc, accent_xft.pixel);
        XFillArc(display, managed->frame, gc, 13, 13, 12, 12, 0, 360 * 64);
        draw_text(managed->frame, name, 35, 25, &text_xft,
                  managed->width - 90);
        XFree(name);
    }

    draw_round_rect(managed->frame, managed->width - 34, 7, 24, 24, 12,
                    index == focused ? close_color : button_color);
    XSetForeground(display, gc, text_xft.pixel);
    XDrawLine(display, managed->frame, gc, managed->width - 27, 14,
              managed->width - 17, 24);
    XDrawLine(display, managed->frame, gc, managed->width - 17, 14,
              managed->width - 27, 24);
    XSetForeground(display, gc, outline_color);
    XDrawLine(display, managed->frame, gc, 0, TITLE_HEIGHT - 1,
              managed->width + RESIZE_BORDER, TITLE_HEIGHT - 1);
    XSetForeground(display, gc, index == focused ? active_color : panel_color);
    XFillRectangle(display, managed->frame, gc, managed->width, TITLE_HEIGHT,
                   RESIZE_BORDER,
                   (unsigned int)(managed->height + RESIZE_BORDER));
    XFillRectangle(display, managed->frame, gc, 0,
                   TITLE_HEIGHT + managed->height,
                   (unsigned int)(managed->width + RESIZE_BORDER),
                   RESIZE_BORDER);
    XSetForeground(display, gc, muted_xft.pixel);
    for (int offset = 3; offset <= 7; offset += 2) {
        XDrawLine(display, managed->frame, gc,
                  managed->width + RESIZE_BORDER - offset,
                  TITLE_HEIGHT + managed->height + RESIZE_BORDER - 1,
                  managed->width + RESIZE_BORDER - 1,
                  TITLE_HEIGHT + managed->height + RESIZE_BORDER - offset);
    }
}

static void focus_client(int index)
{
    if (index < 0 || index >= client_count) {
        return;
    }
    if (focused >= 0 && focused < client_count) {
        draw_frame(focused);
    }
    focused = index;
    XRaiseWindow(display, clients[index].frame);
    XSetInputFocus(display, clients[index].client, RevertToPointerRoot,
                   CurrentTime);
    draw_frame(index);
    draw_taskbar();
    for (size_t i = 0; i < monitor_count; ++i) {
        if (taskbar_enabled && taskbar_windows[i] != None) {
            XRaiseWindow(display, taskbar_windows[i]);
        }
    }
}

static void arrange_client(int index)
{
    ManagedWindow *managed = &clients[index];
    XMoveResizeWindow(display, managed->frame, managed->x, managed->y,
                      (unsigned int)(managed->width + RESIZE_BORDER),
                      (unsigned int)(managed->height + TITLE_HEIGHT +
                                     RESIZE_BORDER));
    XMoveResizeWindow(display, managed->client, 0, TITLE_HEIGHT,
                      (unsigned int)managed->width,
                      (unsigned int)managed->height);
    draw_frame(index);
}

static void manage_window(Window client)
{
    if (find_client(client) >= 0) {
        return;
    }

    if (is_desktop_surface(client)) {
        XMapWindow(display, client);
        XLowerWindow(display, client);
        return;
    }
    if (client_count >= MAX_CLIENTS) {
        return;
    }

    XWindowAttributes attributes;
    if (!XGetWindowAttributes(display, client, &attributes) ||
        attributes.override_redirect) {
        XMapWindow(display, client);
        return;
    }

    int index = client_count++;
    ManagedWindow *managed = &clients[index];
    managed->client = client;
    managed->width = attributes.width < MIN_WIDTH ? MIN_WIDTH : attributes.width;
    managed->height = attributes.height < MIN_HEIGHT ? MIN_HEIGHT : attributes.height;
    MonitorGeometry monitor = monitor_layout[(size_t)next_position %
                                             monitor_count];
    if (attributes.x != 0 || attributes.y != 0) {
        int center_x = attributes.x + attributes.width / 2;
        int center_y = attributes.y + attributes.height / 2;
        for (size_t i = 0; i < monitor_count; ++i) {
            if (center_x >= monitor_layout[i].x &&
                center_x < monitor_layout[i].x + monitor_layout[i].width &&
                center_y >= monitor_layout[i].y &&
                center_y < monitor_layout[i].y + monitor_layout[i].height) {
                monitor = monitor_layout[i];
                break;
            }
        }
    }
    int available_height = monitor.height -
        (taskbar_enabled ? TASKBAR_HEIGHT : 0);
    if (monitor.width - RESIZE_BORDER >= MIN_WIDTH &&
        managed->width > monitor.width - RESIZE_BORDER) {
        managed->width = monitor.width - RESIZE_BORDER;
    }
    if (available_height - TITLE_HEIGHT - RESIZE_BORDER >= MIN_HEIGHT &&
        managed->height >
            available_height - TITLE_HEIGHT - RESIZE_BORDER) {
        managed->height = available_height - TITLE_HEIGHT - RESIZE_BORDER;
    }
    managed->x = attributes.x;
    managed->y = attributes.y;
    if (managed->x == 0 && managed->y == 0) {
        managed->x = monitor.x + 90 + (next_position % 7) * 34;
        managed->y = monitor.y + 36 + (next_position % 6) * 30;
    }
    ++next_position;
    int max_x = monitor.x + monitor.width - managed->width - RESIZE_BORDER;
    int max_y = monitor.y + available_height - managed->height -
                TITLE_HEIGHT - RESIZE_BORDER;
    if (managed->x > max_x) {
        managed->x = max_x > monitor.x ? max_x : monitor.x;
    }
    if (managed->y > max_y) {
        managed->y = max_y > monitor.y ? max_y : monitor.y;
    }

    XSetWindowAttributes frame_attributes;
    frame_attributes.override_redirect = True;
    frame_attributes.background_pixel = frame_color;
    frame_attributes.event_mask = ExposureMask | ButtonPressMask;
    managed->frame = XCreateWindow(display, root, managed->x, managed->y,
                                   (unsigned int)(managed->width + RESIZE_BORDER),
                                   (unsigned int)(managed->height + TITLE_HEIGHT +
                                                  RESIZE_BORDER),
                                   1, CopyFromParent, InputOutput, CopyFromParent,
                                   CWOverrideRedirect | CWBackPixel | CWEventMask,
                                   &frame_attributes);

    XAddToSaveSet(display, client);
    XSelectInput(display, client, StructureNotifyMask | PropertyChangeMask);
    XGrabButton(display, AnyButton, AnyModifier, client, False,
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                GrabModeSync, GrabModeAsync, None, None);
    XSetWindowBorderWidth(display, client, 0);
    XReparentWindow(display, client, managed->frame, 0, TITLE_HEIGHT);
    XMapWindow(display, client);
    XMapWindow(display, managed->frame);
    focus_client(index);
    draw_taskbar();
    if (switcher_window != None) {
        show_switcher();
    }
}

static void forget_client(int index)
{
    if (index < 0 || index >= client_count) {
        return;
    }
    Window frame = clients[index].frame;
    int was_focused = focused == index;
    XDestroyWindow(display, frame);
    for (int i = index; i + 1 < client_count; ++i) {
        clients[i] = clients[i + 1];
    }
    --client_count;
    if (was_focused) {
        focused = index > 0 ? index - 1 : 0;
    } else if (focused > index) {
        --focused;
    }
    if (client_count > 0) {
        focus_client(focused >= 0 && focused < client_count
                         ? focused : client_count - 1);
    } else {
        focused = -1;
        XSetInputFocus(display, root, RevertToPointerRoot, CurrentTime);
    }
    draw_taskbar();
}

static void close_focused(void)
{
    if (focused < 0 || focused >= client_count) {
        return;
    }

    Atom protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    Atom delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    Atom *supported = NULL;
    int count = 0;
    int supports_delete = XGetWMProtocols(display, clients[focused].client,
                                          &supported, &count);
    for (int i = 0; supports_delete && i < count; ++i) {
        if (supported[i] == delete_window) {
            XEvent message;
            memset(&message, 0, sizeof(message));
            message.xclient.type = ClientMessage;
            message.xclient.window = clients[focused].client;
            message.xclient.message_type = protocols;
            message.xclient.format = 32;
            message.xclient.data.l[0] = (long)delete_window;
            message.xclient.data.l[1] = CurrentTime;
            XSendEvent(display, clients[focused].client, False, NoEventMask,
                       &message);
            if (supported != NULL) {
                XFree(supported);
            }
            return;
        }
    }
    if (supported != NULL) {
        XFree(supported);
    }
    XKillClient(display, clients[focused].client);
}

static void draw_taskbar_monitor(size_t monitor_index)
{
    Window window = taskbar_windows[monitor_index];
    int width = monitor_layout[monitor_index].width;
    if (window == None || !taskbar_enabled) {
        return;
    }
    XSetForeground(display, gc, panel_color);
    XFillRectangle(display, window, gc, 0, 0,
                   (unsigned int)width, TASKBAR_HEIGHT);
    XSetForeground(display, gc, outline_color);
    XDrawLine(display, window, gc, 0, 0, width, 0);
    if (client_count == 0) {
        draw_round_rect(window, 17, 14, 18, 18, 6, active_color);
        draw_text(window, "No open applications", 46, 30, &muted_xft, 260);
        return;
    }

    int button_width;
    int shown;
    int offset;
    taskbar_layout(width, &button_width, &shown, &offset);
    for (int i = 0; i < shown; ++i) {
        int x = offset + i * button_width;
        int card_width = button_width - 8;
        draw_round_rect(window, x, 5, card_width, TASKBAR_HEIGHT - 10, 9,
                        i == focused ? button_active_color : button_color);
        if (i == focused) {
            draw_round_rect(window, x + 10, TASKBAR_HEIGHT - 10, card_width - 20,
                            3, 2, accent_xft.pixel);
        }
        char *name = NULL;
        if (!XFetchName(display, clients[i].client, &name) || name == NULL) {
            name = strdup("Application");
        }
        if (name != NULL) {
            unsigned char first_byte = (unsigned char)name[0];
            unsigned long mark_color = i == focused
                ? accent_xft.pixel : muted_xft.pixel;
            XSetForeground(display, gc, mark_color);
            XFillArc(display, window, gc, x + 12, 14, 18, 18,
                     0, 360 * 64);
            XSetForeground(display, gc,
                           i == focused ? button_active_color : button_color);
            size_t mark_length = first_byte < 0x80U ? 1
                : (first_byte & 0xe0U) == 0xc0U ? 2
                : (first_byte & 0xf0U) == 0xe0U ? 3 : 4;
            size_t name_length = strlen(name);
            if (mark_length > name_length) {
                mark_length = name_length;
            }
            char mark_text[5] = {0};
            memcpy(mark_text, name, mark_length);
            if (mark_length == 1 && mark_text[0] >= 'a' &&
                mark_text[0] <= 'z') {
                mark_text[0] -= 'a' - 'A';
            }
            draw_text(window, mark_text, x + 17, 27, &dark_xft, 10);
            draw_text(window, name, x + 38, 29,
                      i == focused ? &text_xft : &muted_xft,
                      card_width - 48);
            XFree(name);
        }
    }
    if (shown < client_count) {
        char overflow[32];
        snprintf(overflow, sizeof(overflow), "+%d more",
                             client_count - shown);
        int x = offset + shown * button_width;
        draw_round_rect(window, x, 5, button_width - 8, TASKBAR_HEIGHT - 10,
                        9, button_color);
        draw_text(window, overflow, x + 10, 29, &muted_xft,
                  button_width - 24);
    }
}

static void draw_taskbar(void)
{
    for (size_t i = 0; i < monitor_count; ++i) {
        draw_taskbar_monitor(i);
    }
}

static void sync_taskbars(void)
{
    MonitorGeometry updated[MAX_MONITORS];
    size_t updated_count = monitor_layout_get(display, root, updated,
                                              MAX_MONITORS);
    if (updated_count == 0) {
        return;
    }
    for (size_t i = updated_count; i < monitor_count; ++i) {
        if (taskbar_windows[i] != None) {
            XDestroyWindow(display, taskbar_windows[i]);
            taskbar_windows[i] = None;
        }
    }
    memcpy(monitor_layout, updated,
           updated_count * sizeof(monitor_layout[0]));
    monitor_count = updated_count;

    XSetWindowAttributes attributes;
    attributes.override_redirect = True;
    attributes.background_pixel = switcher_color;
    attributes.event_mask = ExposureMask | ButtonPressMask;
    for (size_t i = 0; i < monitor_count; ++i) {
        if (taskbar_windows[i] == None) {
            taskbar_windows[i] = XCreateWindow(
                display, root, monitor_layout[i].x,
                monitor_layout[i].y + monitor_layout[i].height - TASKBAR_HEIGHT,
                (unsigned int)monitor_layout[i].width, TASKBAR_HEIGHT, 0,
                CopyFromParent, InputOutput, CopyFromParent,
                CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
        } else {
            XMoveResizeWindow(
                display, taskbar_windows[i], monitor_layout[i].x,
                monitor_layout[i].y + monitor_layout[i].height - TASKBAR_HEIGHT,
                (unsigned int)monitor_layout[i].width, TASKBAR_HEIGHT);
        }
        if (taskbar_enabled) {
            XMapRaised(display, taskbar_windows[i]);
        } else {
            XUnmapWindow(display, taskbar_windows[i]);
        }
    }
    draw_taskbar();
}

static MonitorGeometry monitor_for_client(const ManagedWindow *managed)
{
    int center_x = managed->x + (managed->width + RESIZE_BORDER) / 2;
    int center_y = managed->y +
        (managed->height + TITLE_HEIGHT + RESIZE_BORDER) / 2;
    for (size_t i = 0; i < monitor_count; ++i) {
        MonitorGeometry monitor = monitor_layout[i];
        if (center_x >= monitor.x && center_x < monitor.x + monitor.width &&
            center_y >= monitor.y && center_y < monitor.y + monitor.height) {
            return monitor;
        }
    }
    return monitor_layout[0];
}

static void relayout_for_screen_size(void)
{
    sync_taskbars();
    for (int i = 0; i < client_count; ++i) {
        ManagedWindow *managed = &clients[i];
        MonitorGeometry monitor = monitor_for_client(managed);
        int max_width = monitor.width - RESIZE_BORDER;
        int available_height = monitor.height -
            (taskbar_enabled ? TASKBAR_HEIGHT : 0);
        int max_height = available_height - TITLE_HEIGHT - RESIZE_BORDER;
        if (max_width >= MIN_WIDTH && managed->width > max_width) {
            managed->width = max_width;
        }
        if (max_height >= MIN_HEIGHT && managed->height > max_height) {
            managed->height = max_height;
        }
        int max_x = monitor.x + monitor.width - managed->width - RESIZE_BORDER;
        int max_y = monitor.y + available_height - managed->height -
                    TITLE_HEIGHT - RESIZE_BORDER;
        if (managed->x > max_x) {
            managed->x = max_x > monitor.x ? max_x : monitor.x;
        }
        if (managed->y > max_y) {
            managed->y = max_y > monitor.y ? max_y : monitor.y;
        }
        if (managed->x < monitor.x) {
            managed->x = monitor.x;
        }
        if (managed->y < monitor.y) {
            managed->y = monitor.y;
        }
        arrange_client(i);
    }
    draw_taskbar();
    if (switcher_window != None) {
        show_switcher();
    }
}

static int taskbar_preference_from_root(void)
{
    Atom property = XInternAtom(display, "_FIELD_DESKTOP_FEATURES", False);
    Atom cardinal = XInternAtom(display, "CARDINAL", False);
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *value = NULL;
    int enabled = taskbar_enabled;
    if (XGetWindowProperty(display, root, property, 0, 1, False, cardinal,
                           &actual_type, &actual_format, &item_count,
                           &bytes_after, &value) == Success &&
        value != NULL && actual_format == 32 && item_count == 1) {
        enabled = (((unsigned long *)value)[0] & 1UL) != 0;
    }
    if (value != NULL) {
        XFree(value);
    }
    return enabled;
}

static int taskbar_window_index(Window window)
{
    for (size_t i = 0; i < monitor_count; ++i) {
        if (taskbar_windows[i] == window) {
            return (int)i;
        }
    }
    return -1;
}

static void draw_switcher(void)
{
    if (switcher_window == None) {
        return;
    }
    int visible = client_count < SWITCHER_ROWS ? client_count : SWITCHER_ROWS;
    int first = focused >= SWITCHER_ROWS ? focused - SWITCHER_ROWS + 1 : 0;
    int height = visible * SWITCHER_ROW_HEIGHT + 72;
    XSetForeground(display, gc, switcher_color);
    XFillRectangle(display, switcher_window, gc, 0, 0,
                   SWITCHER_WIDTH, (unsigned int)height);
    draw_text(switcher_window, "Window switcher", 22, 31, &text_xft, 220);
    draw_text(switcher_window, "Alt+Tab to cycle  |  Alt+Esc to close",
              22, 52, &muted_xft, SWITCHER_WIDTH - 44);

    for (int row = 0; row < visible; ++row) {
        int index = first + row;
        int y = 68 + row * SWITCHER_ROW_HEIGHT;
        if (index >= client_count) {
            break;
        }
        draw_round_rect(switcher_window, 12, y - 19,
                        SWITCHER_WIDTH - 24, SWITCHER_ROW_HEIGHT - 4, 8,
                        index == focused ? button_active_color : switcher_color);
        char *name = NULL;
        if (!XFetchName(display, clients[index].client, &name) || name == NULL) {
            name = strdup("Application");
        }
        if (name != NULL) {
            XSetForeground(display, gc,
                           index == focused ? accent_xft.pixel : muted_xft.pixel);
            XFillArc(display, switcher_window, gc, 23, y - 10, 12, 12,
                     0, 360 * 64);
            draw_text(switcher_window, name, 48, y, &text_xft,
                      SWITCHER_WIDTH - 72);
            XFree(name);
        }
    }
}

static void close_switcher(void)
{
    if (switcher_window != None) {
        XDestroyWindow(display, switcher_window);
        switcher_window = None;
    }
}

static void show_switcher(void)
{
    if (client_count == 0) {
        close_switcher();
        return;
    }
    int visible = client_count < SWITCHER_ROWS ? client_count : SWITCHER_ROWS;
    int height = visible * SWITCHER_ROW_HEIGHT + 72;
    MonitorGeometry monitor = monitor_layout[0];
    if (switcher_window == None) {
        XSetWindowAttributes attributes;
        attributes.override_redirect = True;
        attributes.background_pixel = switcher_color;
        attributes.event_mask = ExposureMask | ButtonPressMask;
        switcher_window = XCreateWindow(
            display, root,
            monitor.x + (monitor.width - SWITCHER_WIDTH) / 2,
            monitor.y + (monitor.height - height) / 2,
            SWITCHER_WIDTH, (unsigned int)height, 0,
            CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
        XMapWindow(display, switcher_window);
    }
    XMoveResizeWindow(display, switcher_window,
                      monitor.x + (monitor.width - SWITCHER_WIDTH) / 2,
                      monitor.y + (monitor.height - height) / 2,
                      SWITCHER_WIDTH, (unsigned int)height);
    XRaiseWindow(display, switcher_window);
    draw_switcher();
}

static int report_x_error(Display *connection, XErrorEvent *error)
{
    (void)connection;
    if (error->error_code == BadAccess) {
        fprintf(stderr, "simple-wm: another window manager owns this X display\n");
        startup_failed = 1;
    } else {
        fprintf(stderr, "simple-wm: X11 error %u\n", error->error_code);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int ready_fd = -1;
    if (argc == 3 && strcmp(argv[1], "--ready-fd") == 0) {
        ready_fd = atoi(argv[2]);
    }

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "simple-wm: could not connect to the X11 display\n");
        return 1;
    }
    XSetErrorHandler(report_x_error);
    int screen = DefaultScreen(display);
    root = RootWindow(display, screen);
    Cursor pointer_cursor = XCreateFontCursor(display, XC_left_ptr);
    XDefineCursor(display, root, pointer_cursor);
    XFreeCursor(display, pointer_cursor);
    frame_color = named_color("#12161c");
    active_color = named_color("#252c34");
    switcher_color = named_color("#171c23");
    panel_color = named_color("#1b2028");
    button_color = named_color("#242b34");
    button_active_color = named_color("#303d3a");
    outline_color = named_color("#343d47");
    close_color = named_color("#a94b55");
    gc = XCreateGC(display, root, 0, NULL);
    ui_font = XftFontOpenName(display, screen, "sans:size=10");
    if (ui_font == NULL) {
        ui_font = XftFontOpenName(display, screen, "monospace:size=10");
    }
    if (ui_font == NULL) {
        core_font = XLoadQueryFont(display, "fixed");
        if (core_font != NULL) {
            XSetFont(display, gc, core_font->fid);
        }
        fprintf(stderr, "simple-wm: using the X11 core font fallback\n");
    }
    allocate_text_color("#eef2f6", &text_xft);
    allocate_text_color("#a4afbb", &muted_xft);
    allocate_text_color("#83d6b1", &accent_xft);
    allocate_text_color("#15201c", &dark_xft);
    int randr_available = XRRQueryExtension(display, &randr_event_base,
                                            &randr_error_base);

    XSelectInput(display, root, SubstructureRedirectMask | SubstructureNotifyMask |
                 ButtonPressMask | KeyPressMask | PropertyChangeMask);
    XGrabKey(display, XKeysymToKeycode(display, XK_F4), Mod1Mask, root,
             False, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XK_Tab), Mod1Mask, root,
             False, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XK_Tab),
             Mod1Mask | ShiftMask, root, False, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XK_space), Mod1Mask, root,
             False, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, XKeysymToKeycode(display, XK_Escape), Mod1Mask, root,
             False, GrabModeAsync, GrabModeAsync);
    XSync(display, False);
    if (ready_fd >= 0) {
        char ready = startup_failed ? '0' : '1';
        (void)write(ready_fd, &ready, 1);
        close(ready_fd);
    }
    if (startup_failed) {
        XCloseDisplay(display);
        return 1;
    }
    if (randr_available) {
        long event_mask = monitor_layout_event_mask(display);
        if (event_mask != 0) {
            XRRSelectInput(display, root, event_mask);
        }
    }
    taskbar_enabled = taskbar_preference_from_root();
    sync_taskbars();

    XEvent event;
    Window moving_frame = None;
    int drag_mode = 0;
    int drag_root_x = 0;
    int drag_root_y = 0;
    int origin_x = 0;
    int origin_y = 0;
    int origin_width = 0;
    int origin_height = 0;

    for (;;) {
        XNextEvent(display, &event);
        if (randr_available &&
            (event.type == randr_event_base + RRScreenChangeNotify ||
             event.type == randr_event_base + RRNotify)) {
            if (event.type == randr_event_base + RRScreenChangeNotify) {
                XRRUpdateConfiguration(&event);
            }
            relayout_for_screen_size();
        } else if (event.type == PropertyNotify &&
                   event.xproperty.window == root &&
                   event.xproperty.atom ==
                       XInternAtom(display, "_FIELD_DESKTOP_FEATURES", False)) {
            int enabled = taskbar_preference_from_root();
            if (enabled != taskbar_enabled) {
                taskbar_enabled = enabled;
                relayout_for_screen_size();
            }
        } else if (event.type == MapRequest) {
            manage_window(event.xmaprequest.window);
        } else if (event.type == ConfigureRequest) {
            int index = find_client(event.xconfigurerequest.window);
            if (index < 0) {
                XWindowChanges changes;
                changes.x = event.xconfigurerequest.x;
                changes.y = event.xconfigurerequest.y;
                changes.width = event.xconfigurerequest.width;
                changes.height = event.xconfigurerequest.height;
                changes.border_width = event.xconfigurerequest.border_width;
                changes.sibling = event.xconfigurerequest.above;
                changes.stack_mode = event.xconfigurerequest.detail;
                XConfigureWindow(display, event.xconfigurerequest.window,
                                 event.xconfigurerequest.value_mask, &changes);
            } else {
                ManagedWindow *managed = &clients[index];
                if (event.xconfigurerequest.value_mask & CWWidth) {
                    managed->width = event.xconfigurerequest.width < MIN_WIDTH
                        ? MIN_WIDTH : event.xconfigurerequest.width;
                }
                if (event.xconfigurerequest.value_mask & CWHeight) {
                    managed->height = event.xconfigurerequest.height < MIN_HEIGHT
                        ? MIN_HEIGHT : event.xconfigurerequest.height;
                }
                arrange_client(index);
            }
        } else if (event.type == DestroyNotify) {
            int index = find_client(event.xdestroywindow.window);
            if (index >= 0 && clients[index].client == event.xdestroywindow.window) {
                forget_client(index);
                if (switcher_window != None) {
                    show_switcher();
                }
            }
        } else if (event.type == Expose && event.xexpose.count == 0 &&
                   event.xexpose.window == switcher_window) {
            draw_switcher();
        } else if (event.type == Expose && event.xexpose.count == 0 &&
                   taskbar_window_index(event.xexpose.window) >= 0) {
            draw_taskbar_monitor(
                (size_t)taskbar_window_index(event.xexpose.window));
        } else if (event.type == Expose && event.xexpose.count == 0) {
            int index = find_client(event.xexpose.window);
            if (index >= 0) {
                draw_frame(index);
            }
        } else if (event.type == PropertyNotify) {
            int index = find_client(event.xproperty.window);
            if (index >= 0 && clients[index].client == event.xproperty.window) {
                draw_frame(index);
                draw_taskbar();
            }
        } else if (event.type == ButtonPress) {
            if (switcher_window != None &&
                event.xbutton.window != switcher_window) {
                close_switcher();
            }
            int index = find_client(event.xbutton.window);
            int taskbar_index = taskbar_window_index(event.xbutton.window);
            if (taskbar_index >= 0) {
                if (event.xbutton.button != Button1) {
                    continue;
                }
                int screen_width = monitor_layout[taskbar_index].width;
                if (client_count > 0) {
                    int button_width;
                    int shown;
                    int offset;
                    taskbar_layout(screen_width, &button_width, &shown,
                                   &offset);
                    int selected =
                        (event.xbutton.x - offset) / button_width;
                    if (event.xbutton.x >= offset && selected >= 0 &&
                        selected < shown && selected < client_count) {
                        focus_client(selected);
                    } else if (event.xbutton.x >=
                               offset + shown * button_width) {
                        show_switcher();
                    }
                }
                continue;
            }
            if (event.xbutton.window == switcher_window &&
                switcher_window != None) {
                if (event.xbutton.button != Button1 ||
                    event.xbutton.y < 49) {
                    continue;
                }
                int visible = client_count < SWITCHER_ROWS
                    ? client_count : SWITCHER_ROWS;
                int first = focused >= SWITCHER_ROWS
                    ? focused - SWITCHER_ROWS + 1 : 0;
                int row = (event.xbutton.y - 49) / SWITCHER_ROW_HEIGHT;
                int selected = first + row;
                if (row >= 0 && row < visible && selected < client_count) {
                    focus_client(selected);
                    close_switcher();
                }
                continue;
            }
            if (index < 0) {
                continue;
            }
            focus_client(index);
            if (event.xbutton.window == clients[index].client) {
                int alt_drag = (event.xbutton.state & Mod1Mask) != 0 &&
                               (event.xbutton.button == Button1 ||
                                event.xbutton.button == Button3);
                if (alt_drag) {
                    moving_frame = clients[index].frame;
                    drag_mode = event.xbutton.button == Button3 ? 3 : 0;
                    drag_root_x = event.xbutton.x_root;
                    drag_root_y = event.xbutton.y_root;
                    origin_x = clients[index].x;
                    origin_y = clients[index].y;
                    origin_width = clients[index].width;
                    origin_height = clients[index].height;
                    XAllowEvents(display, AsyncPointer, event.xbutton.time);
                } else {
                    XAllowEvents(display, ReplayPointer, event.xbutton.time);
                }
                continue;
            }
            int title_click = event.xbutton.window == clients[index].frame &&
                              event.xbutton.y < TITLE_HEIGHT;
            int close_click = title_click &&
                event.xbutton.x >= clients[index].width - 40 &&
                event.xbutton.x < clients[index].width;
            if (close_click && event.xbutton.button == Button1) {
                close_focused();
                continue;
            }
            int alt_drag = (event.xbutton.state & Mod1Mask) != 0 &&
                           (event.xbutton.button == Button1 ||
                            event.xbutton.button == Button3);
            if (title_click || alt_drag ||
                event.xbutton.window == clients[index].frame) {
                if (!alt_drag && event.xbutton.button != Button1) {
                    continue;
                }
                moving_frame = clients[index].frame;
                if (alt_drag) {
                    drag_mode = event.xbutton.button == Button3 ? 3 : 0;
                } else if (!title_click) {
                    int resize_width = event.xbutton.x >= clients[index].width;
                    int resize_height = event.xbutton.y >=
                        TITLE_HEIGHT + clients[index].height;
                    drag_mode = (resize_width ? 1 : 0) |
                                (resize_height ? 2 : 0);
                } else {
                    drag_mode = 0;
                }
                drag_root_x = event.xbutton.x_root;
                drag_root_y = event.xbutton.y_root;
                origin_x = clients[index].x;
                origin_y = clients[index].y;
                origin_width = clients[index].width;
                origin_height = clients[index].height;
                XGrabPointer(display, event.xbutton.window, False,
                             PointerMotionMask | ButtonReleaseMask,
                             GrabModeAsync, GrabModeAsync, None, None,
                             event.xbutton.time);
            }
        } else if (event.type == MotionNotify && moving_frame != None) {
            int index = find_client(moving_frame);
            if (index >= 0) {
                int dx = event.xmotion.x_root - drag_root_x;
                int dy = event.xmotion.y_root - drag_root_y;
                if (drag_mode != 0) {
                    if ((drag_mode & 1) != 0) {
                        clients[index].width = origin_width + dx < MIN_WIDTH
                            ? MIN_WIDTH : origin_width + dx;
                    }
                    if ((drag_mode & 2) != 0) {
                        clients[index].height = origin_height + dy < MIN_HEIGHT
                            ? MIN_HEIGHT : origin_height + dy;
                    }
                } else {
                    clients[index].x = origin_x + dx;
                    clients[index].y = origin_y + dy;
                }
                arrange_client(index);
            }
        } else if (event.type == ButtonRelease) {
            if (moving_frame != None) {
                XUngrabPointer(display, event.xbutton.time);
                moving_frame = None;
            }
        } else if (event.type == KeyPress) {
            if (event.xkey.keycode == XKeysymToKeycode(display, XK_F4)) {
                close_focused();
            } else if (event.xkey.keycode == XKeysymToKeycode(display, XK_Tab) &&
                       client_count > 0) {
                int direction = (event.xkey.state & ShiftMask) != 0 ? -1 : 1;
                int next = (focused + direction + client_count) % client_count;
                focus_client(next);
                show_switcher();
            } else if (event.xkey.keycode ==
                       XKeysymToKeycode(display, XK_space)) {
                close_switcher();
                Atom owner = XInternAtom(display,
                                         "_FIELD_DESKTOP_COMMAND_OWNER", False);
                Atom command = XInternAtom(display,
                                           "_FIELD_DESKTOP_COMMAND", False);
                Window desktop = XGetSelectionOwner(display, owner);
                if (desktop != None) {
                    XEvent message;
                    memset(&message, 0, sizeof(message));
                    message.xclient.type = ClientMessage;
                    message.xclient.window = desktop;
                    message.xclient.message_type = command;
                    message.xclient.format = 32;
                    message.xclient.data.l[0] = 1;
                    XSendEvent(display, desktop, False, NoEventMask, &message);
                }
            } else if (event.xkey.keycode ==
                       XKeysymToKeycode(display, XK_Escape)) {
                close_switcher();
            }
        }
    }

    close_switcher();
    return 0;
}