#define _POSIX_C_SOURCE 200809L

#include "gtk_shell.h"
#include "gtk_shell_internal.h"
#include "launcher.h"
#include "settings.h"

#include <glib-unix.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

void shell_show_error(GtkWindow *parent, const char *message)
{
    GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", message);
    gtk_alert_dialog_show(dialog, parent);
    g_object_unref(dialog);
}

void shell_update_styles(ShellState *state)
{
    const AppearanceTheme *theme =
        appearance_theme_at((size_t)state->appearance.theme);
    if (theme == NULL) {
        fprintf(stderr, "Invalid appearance theme selection.\n");
        return;
    }
    const char *background = appearance_background_color(
        state->appearance.background);
    if (background == NULL) {
        background = theme->background;
    }
    char css[2048];
    int length = snprintf(
        css, sizeof(css),
        "window { color: %s; background-color: %s; }"
        ".desktop-surface { background-color: transparent; }"
        ".panel { background-color: %s; border-bottom: 1px solid "
        "alpha(%s, 0.12); padding: 8px 16px; }"
        ".brand { font-weight: 700; letter-spacing: 0.04em; }"
        ".muted { color: alpha(%s, 0.72); }"
        "button { border-radius: 9px; padding: 8px 12px; }"
        ".desktop-shortcut { background-color: transparent; border: 0;"
        " padding: 12px 8px; }"
        ".desktop-shortcut:hover { background-color: alpha(%s, 0.12); }"
        ".primary-action { background-color: %s; color: %s; font-weight: 600; }"
        "entry, list, notebook { border-radius: 10px; }",
        theme->foreground, background, theme->panel, theme->foreground,
        theme->foreground, theme->foreground, theme->accent, theme->background);
    if (length < 0 || (size_t)length >= sizeof(css)) {
        fprintf(stderr, "Could not build desktop theme stylesheet.\n");
        return;
    }
    gtk_css_provider_load_from_string(state->styles, css);
}

static void settings_path(char *path, size_t size)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        path[0] = '\0';
        return;
    }
    int length = snprintf(path, size, "%s/.config/simple-desktop/features",
                         home);
    if (length < 0 || (size_t)length >= size) {
        path[0] = '\0';
    }
}

static void load_settings(ShellState *state)
{
    char path[1200];
    settings_path(path, sizeof(path));
    FILE *file = path[0] != '\0' ? fopen(path, "r") : NULL;
    if (file == NULL) {
        return;
    }
    char line[128];
    int enabled;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "top_panel=%d", &enabled) == 1 &&
            (enabled == 0 || enabled == 1)) {
            state->top_panel = enabled;
        } else if (sscanf(line, "desktop_files=%d", &enabled) == 1 &&
                   (enabled == 0 || enabled == 1)) {
            state->desktop_files = enabled;
        } else if (sscanf(line, "taskbar=%d", &enabled) == 1 &&
                   (enabled == 0 || enabled == 1)) {
            state->taskbar = enabled;
        }
    }
    fclose(file);
}

void shell_publish_features(ShellState *state)
{
    unsigned long features = state->taskbar ? 1UL : 0UL;
    XChangeProperty(state->xdisplay, DefaultRootWindow(state->xdisplay),
                    state->feature_atom, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&features, 1);
    XFlush(state->xdisplay);
}

int shell_save_settings(ShellState *state)
{
    shell_publish_features(state);
    char path[1200];
    settings_path(path, sizeof(path));
    if (path[0] == '\0') {
        fprintf(stderr, "Cannot save desktop settings without a home directory.\n");
        return 0;
    }
    const char *home = getenv("HOME");
    char config_directory[1200];
    int config_length = snprintf(config_directory, sizeof(config_directory),
                                 "%s/.config", home);
    if (config_length < 0 || (size_t)config_length >= sizeof(config_directory) ||
        (mkdir(config_directory, 0700) != 0 && errno != EEXIST)) {
        perror("Could not create user configuration directory");
        return 0;
    }
    char directory[1200];
    int length = snprintf(directory, sizeof(directory),
                          "%s/.config/simple-desktop", home);
    if (length < 0 || (size_t)length >= sizeof(directory) ||
        (mkdir(directory, 0700) != 0 && errno != EEXIST)) {
        perror("Could not create desktop settings directory");
        return 0;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        perror("Could not save desktop settings");
        return 0;
    }
    int failed = fprintf(file, "top_panel=%d\ndesktop_files=%d\ntaskbar=%d\n",
                         state->top_panel, state->desktop_files,
                         state->taskbar) < 0;
    if (fclose(file) != 0) {
        failed = 1;
    }
    if (failed) {
        perror("Could not write desktop settings");
        return 0;
    }
    return 1;
}

static int launch_terminal(void)
{
    static const char *const terminals[] = {
        "xfce4-terminal", "konsole", "xterm", "gnome-terminal"
    };
    for (size_t i = 0; i < sizeof(terminals) / sizeof(terminals[0]); ++i) {
        char *arguments[] = {(char *)terminals[i], NULL};
        GError *error = NULL;
        if (g_spawn_async(NULL, arguments, NULL, G_SPAWN_SEARCH_PATH,
                          NULL, NULL, NULL, &error)) {
            return 1;
        }
        g_clear_error(&error);
    }
    fprintf(stderr, "No supported terminal emulator is installed.\n");
    return 0;
}

int shell_launch_action(ShellState *state, int action)
{
    switch (action) {
    case SHELL_ACTION_APPS:
        launcher_open(state);
        return 1;
    case SHELL_ACTION_TERMINAL:
        return launch_terminal();
    case SHELL_ACTION_FILES:
        return applications_launch_kind(state->applications,
                                        state->application_count,
                                        APPLICATION_FILE_MANAGER);
    case SHELL_ACTION_BROWSER:
        return applications_launch_kind(state->applications,
                                        state->application_count,
                                        APPLICATION_BROWSER);
    case SHELL_ACTION_SETTINGS:
        settings_open(state);
        return 1;
    default:
        return 0;
    }
}

static gboolean desktop_items_changed(gint fd, GIOCondition condition,
                                      gpointer data)
{
    ShellState *state = data;
    if ((condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) != 0) {
        fprintf(stderr, "Desktop folder watcher stopped unexpectedly.\n");
        state->items_source = 0;
        return G_SOURCE_REMOVE;
    }
    if ((condition & G_IO_IN) != 0 &&
        desktop_items_handle_changes(&state->desktop_items)) {
        shell_refresh_desktop_windows(state);
    }
    (void)fd;
    return G_SOURCE_CONTINUE;
}

static void process_x_events(ShellState *state)
{
    while (XPending(state->xdisplay) > 0) {
        XEvent event;
        XNextEvent(state->xdisplay, &event);
        if (event.type == ClientMessage &&
            event.xclient.window == state->command_window &&
            event.xclient.message_type == state->command_atom &&
            event.xclient.data.l[0] == 1) {
            launcher_open(state);
        }
    }
}

static gboolean x_events_ready(gint fd, GIOCondition condition, gpointer data)
{
    ShellState *state = data;
    if ((condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) != 0) {
        fprintf(stderr, "X11 connection closed.\n");
        state->x_source = 0;
        g_application_quit(G_APPLICATION(state->application));
        return G_SOURCE_REMOVE;
    }
    process_x_events(state);
    (void)fd;
    return G_SOURCE_CONTINUE;
}

static gboolean refresh_monitors(gpointer data)
{
    ShellState *state = data;
    MonitorGeometry current[SHELL_MAX_MONITORS];
    size_t count = monitor_layout_get(state->xdisplay,
                                      DefaultRootWindow(state->xdisplay),
                                      current, SHELL_MAX_MONITORS);
    int changed = count != state->monitor_count;
    for (size_t i = 0; !changed && i < count; ++i) {
        changed = memcmp(&current[i], &state->monitors[i],
                         sizeof(current[i])) != 0;
    }
    if (changed) {
        shell_refresh_desktop_windows(state);
    }
    return G_SOURCE_CONTINUE;
}

static void window_manager_exited(GPid pid, gint status, gpointer data)
{
    ShellState *state = data;
    state->window_manager = -1;
    state->window_manager_failed = 1;
    state->wm_source = 0;
    if (WIFEXITED(status)) {
        fprintf(stderr, "The window manager exited with status %d.\n",
                WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "The window manager stopped on signal %d.\n",
                WTERMSIG(status));
    } else {
        fprintf(stderr, "The window manager stopped unexpectedly.\n");
    }
    g_spawn_close_pid(pid);
    g_application_quit(G_APPLICATION(state->application));
}

static void create_command_window(ShellState *state)
{
    int screen = DefaultScreen(state->xdisplay);
    Window root = RootWindow(state->xdisplay, screen);
    state->command_window = XCreateSimpleWindow(
        state->xdisplay, root, -2, -2, 1, 1, 0, 0, 0);
    state->command_atom = XInternAtom(state->xdisplay,
                                      "_FIELD_DESKTOP_COMMAND", False);
    state->owner_atom = XInternAtom(state->xdisplay,
                                    "_FIELD_DESKTOP_COMMAND_OWNER", False);
    state->feature_atom = XInternAtom(state->xdisplay,
                                      "_FIELD_DESKTOP_FEATURES", False);
    state->desktop_type_atom = XInternAtom(
        state->xdisplay, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    XSetSelectionOwner(state->xdisplay, state->owner_atom,
                       state->command_window, CurrentTime);
    if (XGetSelectionOwner(state->xdisplay, state->owner_atom) !=
        state->command_window) {
        fprintf(stderr, "Could not claim the desktop command selection.\n");
        state->command_selection_failed = 1;
        g_application_quit(G_APPLICATION(state->application));
    }
    XSelectInput(state->xdisplay, root, PropertyChangeMask |
                 StructureNotifyMask);
    int event_base;
    int error_base;
    if (XRRQueryExtension(state->xdisplay, &event_base, &error_base)) {
        long event_mask = monitor_layout_event_mask(state->xdisplay);
        if (event_mask != 0) {
            XRRSelectInput(state->xdisplay, root, event_mask);
        }
    }
    shell_publish_features(state);
}

static void application_activate(GtkApplication *application, gpointer data)
{
    ShellState *state = data;
    state->application = application;
    state->styles = gtk_css_provider_new();
    shell_update_styles(state);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(state->styles),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    shell_refresh_desktop_windows(state);
    create_command_window(state);
    state->x_source = g_unix_fd_add(
        ConnectionNumber(state->xdisplay),
        G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL, x_events_ready, state);
    if (state->desktop_items.watch_fd >= 0) {
        state->items_source = g_unix_fd_add(
            state->desktop_items.watch_fd,
            G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
            desktop_items_changed, state);
    }
    state->monitor_source = g_timeout_add_seconds(2, refresh_monitors, state);
    if (state->window_manager > 0) {
        state->wm_source = g_child_watch_add(
            state->window_manager, window_manager_exited, state);
    }
}

int gtk_shell_run(int argc, char **argv, pid_t window_manager)
{
    gdk_set_allowed_backends("x11");
    ShellState *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        perror("Could not allocate desktop shell state");
        return 1;
    }
    state->top_panel = 1;
    state->desktop_files = 1;
    state->taskbar = 1;
    state->window_manager = window_manager;
    state->xdisplay = XOpenDisplay(NULL);
    if (state->xdisplay == NULL) {
        fprintf(stderr, "Could not connect to the X11 display.\n");
        free(state);
        return 1;
    }
    load_settings(state);
    appearance_init(&state->appearance);
    state->application_count = applications_load(
        state->applications, SHELL_MAX_APPLICATIONS);
    if (!desktop_items_init(&state->desktop_items, SHELL_MAX_DESKTOP_ITEMS)) {
        memset(&state->desktop_items, 0, sizeof(state->desktop_items));
        state->desktop_items.watch_fd = -1;
        state->desktop_items.watch_id = -1;
    }

    GtkApplication *application = gtk_application_new(
        "org.zirtualz.field-desktop", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(application, "activate",
                     G_CALLBACK(application_activate), state);
    char *gtk_argv[] = {argv[0], NULL};
    int result = g_application_run(G_APPLICATION(application), 1, gtk_argv);

    if (state->x_source != 0) {
        g_source_remove(state->x_source);
    }
    if (state->items_source != 0) {
        g_source_remove(state->items_source);
    }
    if (state->monitor_source != 0) {
        g_source_remove(state->monitor_source);
    }
    if (state->wm_source != 0) {
        g_source_remove(state->wm_source);
    }
    launcher_close(state);
    if (state->settings != NULL) {
        gtk_window_destroy(GTK_WINDOW(state->settings));
    }
    for (size_t i = 0; i < SHELL_MAX_MONITORS; ++i) {
        if (state->desktop_windows[i] != NULL) {
            gtk_window_destroy(GTK_WINDOW(state->desktop_windows[i]));
        }
    }
    if (state->command_window != None) {
        if (XGetSelectionOwner(state->xdisplay, state->owner_atom) ==
            state->command_window) {
            XSetSelectionOwner(state->xdisplay, state->owner_atom, None,
                               CurrentTime);
        }
        XDestroyWindow(state->xdisplay, state->command_window);
    }
    if (state->desktop_items.items != NULL) {
        desktop_items_destroy(&state->desktop_items);
    }
    g_clear_object(&state->styles);
    g_clear_object(&application);
    XCloseDisplay(state->xdisplay);
    result = state->window_manager_failed || state->command_selection_failed
        ? 1 : result;
    free(state);
    (void)argc;
    return result;
}
