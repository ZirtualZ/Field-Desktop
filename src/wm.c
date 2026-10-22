#define _POSIX_C_SOURCE 200809L

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
    TITLE_HEIGHT = 28,
    MIN_WIDTH = 240,
    MIN_HEIGHT = 140,
    RESIZE_BORDER = 8,
    SWITCHER_ROWS = 10,
    SWITCHER_WIDTH = 420,
    SWITCHER_ROW_HEIGHT = 30,
    TASKBAR_HEIGHT = 40,
    TASKBAR_BUTTON_MIN_WIDTH = 96
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
static unsigned long title_color;
static unsigned long switcher_color;
static GC gc;
static int startup_failed;
static Window switcher_window;
static Window taskbar_window;
static int randr_event_base;
static int randr_error_base;

static void show_switcher(void);
static void draw_taskbar(void);

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

static int find_client(Window window)
{
    for (int i = 0; i < client_count; ++i) {
        if (clients[i].client == window || clients[i].frame == window) {
            return i;
        }
    }
    return -1;
}

static void draw_frame(int index)
{
    ManagedWindow *managed = &clients[index];
    XSetForeground(display, gc, index == focused ? active_color : frame_color);
    XFillRectangle(display, managed->frame, gc, 0, 0,
                   (unsigned int)(managed->width + RESIZE_BORDER),
                   TITLE_HEIGHT);

    char *name = NULL;
    if (!XFetchName(display, managed->client, &name) || name == NULL) {
        name = strdup("Application");
    }
    XSetForeground(display, gc, title_color);
    if (name != NULL) {
        XDrawString(display, managed->frame, gc, 12, 19, name,
                    (int)strlen(name) > 48 ? 48 : (int)strlen(name));
        XFree(name);
    }

    XSetForeground(display, gc, title_color);
    XDrawLine(display, managed->frame, gc, managed->width - 31, 9,
              managed->width - 15, 9);
    XDrawLine(display, managed->frame, gc, managed->width - 31, 17,
              managed->width - 15, 17);

    XSetForeground(display, gc, index == focused ? active_color : frame_color);
    XFillRectangle(display, managed->frame, gc, managed->width, TITLE_HEIGHT,
                   RESIZE_BORDER,
                   (unsigned int)(managed->height + RESIZE_BORDER));
    XFillRectangle(display, managed->frame, gc, 0,
                   TITLE_HEIGHT + managed->height,
                   (unsigned int)(managed->width + RESIZE_BORDER),
                   RESIZE_BORDER);
    XSetForeground(display, gc, title_color);
    for (int offset = 2; offset <= 6; offset += 2) {
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
    if (taskbar_window != None) {
        XRaiseWindow(display, taskbar_window);
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
    if (find_client(client) >= 0 || client_count >= MAX_CLIENTS) {
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
    managed->x = 90 + (next_position % 7) * 34;
    managed->y = 76 + (next_position % 6) * 30;
    ++next_position;

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
    if (focused == index) {
        focused = -1;
    }
    XDestroyWindow(display, frame);
    for (int i = index; i + 1 < client_count; ++i) {
        clients[i] = clients[i + 1];
    }
    --client_count;
    if (focused > index) {
        --focused;
    }
    if (client_count > 0) {
        focus_client(focused >= 0 ? focused : client_count - 1);
    } else {
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

static void draw_taskbar(void)
{
    if (taskbar_window == None) {
        return;
    }
    int screen = DefaultScreen(display);
    int width = DisplayWidth(display, screen);
    XSetForeground(display, gc, switcher_color);
    XFillRectangle(display, taskbar_window, gc, 0, 0,
                   (unsigned int)width, TASKBAR_HEIGHT);
    if (client_count == 0) {
        XSetForeground(display, gc, title_color);
        XDrawString(display, taskbar_window, gc, 16, 25,
                    "No open windows", 15);
        return;
    }

    int button_width = width / client_count;
    if (button_width < 1) {
        button_width = 1;
    }
    if (button_width > 240) {
        button_width = 240;
    }
    int shown = width / button_width;
    if (shown > client_count) {
        shown = client_count;
    }
    int offset = shown == client_count
        ? (width - shown * button_width) / 2 : 0;
    for (int i = 0; i < shown; ++i) {
        int x = offset + i * button_width;
        if (i == focused) {
            XSetForeground(display, gc, active_color);
            XFillRectangle(display, taskbar_window, gc, x + 2, 2,
                           (unsigned int)(button_width > 4
                               ? button_width - 4 : 1),
                           TASKBAR_HEIGHT - 4);
        }
        char *name = NULL;
        if (!XFetchName(display, clients[i].client, &name) || name == NULL) {
            name = strdup("Application");
        }
        XSetForeground(display, gc, title_color);
        if (name != NULL) {
            int max_chars = (button_width - 20) / 8;
            if (max_chars < 0) {
                max_chars = 0;
            }
            int length = (int)strlen(name);
            XDrawString(display, taskbar_window, gc, x + 12, 25, name,
                        length > max_chars ? max_chars : length);
            XFree(name);
        }
    }
    if (shown < client_count) {
        char overflow[32];
        int length = snprintf(overflow, sizeof(overflow), "+%d more",
                             client_count - shown);
        XSetForeground(display, gc, title_color);
        XDrawString(display, taskbar_window, gc,
                    offset + shown * button_width + 8, 25, overflow, length);
    }
}

static void relayout_for_screen_size(void)
{
    int screen = DefaultScreen(display);
    int screen_width = DisplayWidth(display, screen);
    int screen_height = DisplayHeight(display, screen);
    XMoveResizeWindow(display, taskbar_window, 0,
                      screen_height - TASKBAR_HEIGHT,
                      (unsigned int)screen_width, TASKBAR_HEIGHT);
    for (int i = 0; i < client_count; ++i) {
        ManagedWindow *managed = &clients[i];
        int max_width = screen_width - RESIZE_BORDER;
        int max_height = screen_height - TITLE_HEIGHT - RESIZE_BORDER;
        if (max_width >= MIN_WIDTH && managed->width > max_width) {
            managed->width = max_width;
        }
        if (max_height >= MIN_HEIGHT && managed->height > max_height) {
            managed->height = max_height;
        }
        int max_x = screen_width - managed->width - RESIZE_BORDER;
        int max_y = screen_height - managed->height - TITLE_HEIGHT -
                    RESIZE_BORDER;
        if (managed->x > max_x) {
            managed->x = max_x > 0 ? max_x : 0;
        }
        if (managed->y > max_y) {
            managed->y = max_y > 0 ? max_y : 0;
        }
        if (managed->x < 0) {
            managed->x = 0;
        }
        if (managed->y < 0) {
            managed->y = 0;
        }
        arrange_client(i);
    }
    draw_taskbar();
    if (switcher_window != None) {
        show_switcher();
    }
}

static void draw_switcher(void)
{
    if (switcher_window == None) {
        return;
    }
    int visible = client_count < SWITCHER_ROWS ? client_count : SWITCHER_ROWS;
    int first = focused >= SWITCHER_ROWS ? focused - SWITCHER_ROWS + 1 : 0;
    int height = visible * SWITCHER_ROW_HEIGHT + 44;
    XSetForeground(display, gc, switcher_color);
    XFillRectangle(display, switcher_window, gc, 0, 0,
                   SWITCHER_WIDTH, (unsigned int)height);
    XSetForeground(display, gc, title_color);
    const char *header = "OPEN WINDOWS  |  Alt+Tab to cycle  |  Alt+Esc to close";
    XDrawString(display, switcher_window, gc, 18, 26,
                header, (int)strlen(header));

    for (int row = 0; row < visible; ++row) {
        int index = first + row;
        int y = 44 + row * SWITCHER_ROW_HEIGHT;
        if (index >= client_count) {
            break;
        }
        if (index == focused) {
            XSetForeground(display, gc, active_color);
            XFillRectangle(display, switcher_window, gc, 10, y - 19,
                           SWITCHER_WIDTH - 20, SWITCHER_ROW_HEIGHT);
        }
        XSetForeground(display, gc, title_color);
        char *name = NULL;
        if (!XFetchName(display, clients[index].client, &name) || name == NULL) {
            name = strdup("Application");
        }
        if (name != NULL) {
            int length = (int)strlen(name);
            XDrawString(display, switcher_window, gc, 20, y, name,
                        length > 52 ? 52 : length);
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
    int screen = DefaultScreen(display);
    int visible = client_count < SWITCHER_ROWS ? client_count : SWITCHER_ROWS;
    int height = visible * SWITCHER_ROW_HEIGHT + 44;
    if (switcher_window == None) {
        XSetWindowAttributes attributes;
        attributes.override_redirect = True;
        attributes.background_pixel = switcher_color;
        attributes.event_mask = ExposureMask | ButtonPressMask;
        switcher_window = XCreateWindow(
            display, root,
            (DisplayWidth(display, screen) - SWITCHER_WIDTH) / 2,
            (DisplayHeight(display, screen) - height) / 2,
            SWITCHER_WIDTH, (unsigned int)height, 0,
            CopyFromParent, InputOutput, CopyFromParent,
            CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
        XMapWindow(display, switcher_window);
    }
    XMoveResizeWindow(display, switcher_window,
                      (DisplayWidth(display, screen) - SWITCHER_WIDTH) / 2,
                      (DisplayHeight(display, screen) - height) / 2,
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
    frame_color = named_color("#24312d");
    active_color = named_color("#4c8068");
    title_color = named_color("#f0f1e9");
    switcher_color = named_color("#25332e");
    gc = XCreateGC(display, root, 0, NULL);
    int randr_available = XRRQueryExtension(display, &randr_event_base,
                                            &randr_error_base);

    XSelectInput(display, root, SubstructureRedirectMask | SubstructureNotifyMask |
                 ButtonPressMask | KeyPressMask);
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
        XRRSelectInput(display, root, RRScreenChangeNotifyMask);
    }

    XSetWindowAttributes taskbar_attributes;
    taskbar_attributes.override_redirect = True;
    taskbar_attributes.background_pixel = switcher_color;
    taskbar_attributes.event_mask = ExposureMask | ButtonPressMask;
    taskbar_window = XCreateWindow(
        display, root, 0, DisplayHeight(display, screen) - TASKBAR_HEIGHT,
        (unsigned int)DisplayWidth(display, screen), TASKBAR_HEIGHT, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &taskbar_attributes);
    XMapRaised(display, taskbar_window);
    draw_taskbar();

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
            event.type == randr_event_base + RRScreenChangeNotify) {
            XRRUpdateConfiguration(&event);
            relayout_for_screen_size();
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
                   event.xexpose.window == taskbar_window) {
            draw_taskbar();
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
            if (event.xbutton.window == taskbar_window) {
                int screen_width = DisplayWidth(display, DefaultScreen(display));
                if (client_count > 0) {
                    int button_width = screen_width / client_count;
                    if (button_width < 1) {
                        button_width = 1;
                    }
                    if (button_width > 240) {
                        button_width = 240;
                    }
                    int shown = screen_width / button_width;
                    if (shown > client_count) {
                        shown = client_count;
                    }
                    int offset = shown == client_count
                        ? (screen_width - shown * button_width) / 2 : 0;
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
                int visible = client_count < SWITCHER_ROWS
                    ? client_count : SWITCHER_ROWS;
                int first = focused >= SWITCHER_ROWS
                    ? focused - SWITCHER_ROWS + 1 : 0;
                int row = (event.xbutton.y - 25) / SWITCHER_ROW_HEIGHT;
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
            if (close_click) {
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