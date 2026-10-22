#ifndef FIELD_DESKTOP_SHELL_GTK_SHELL_INTERNAL_H
#define FIELD_DESKTOP_SHELL_GTK_SHELL_INTERNAL_H

#include "../applications.h"
#include "../appearance.h"
#include "../desktop_items.h"
#include "../monitor_layout.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <gtk/gtk.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <X11/Xlib.h>
#include <sys/types.h>

enum {
    SHELL_MAX_APPLICATIONS = 512,
    SHELL_MAX_MONITORS = 16,
    SHELL_MAX_DESKTOP_ITEMS = 256,
    SHELL_ACTION_APPS,
    SHELL_ACTION_TERMINAL,
    SHELL_ACTION_FILES,
    SHELL_ACTION_BROWSER,
    SHELL_ACTION_SETTINGS
};

typedef struct {
    GtkApplication *application;
    Display *xdisplay;
    Window command_window;
    Atom command_atom;
    Atom owner_atom;
    Atom feature_atom;
    Atom desktop_type_atom;
    Application applications[SHELL_MAX_APPLICATIONS];
    size_t application_count;
    DesktopItems desktop_items;
    Appearance appearance;
    MonitorGeometry monitors[SHELL_MAX_MONITORS];
    size_t monitor_count;
    GtkWidget *desktop_windows[SHELL_MAX_MONITORS];
    GtkWidget *wallpaper_pictures[SHELL_MAX_MONITORS];
    GtkWidget *launcher;
    GtkWidget *settings;
    GtkWidget *launcher_entry;
    GtkWidget *launcher_results;
    GtkWidget *wallpaper_toggle;
    GtkCssProvider *styles;
    int command_selection_failed;
    int top_panel;
    int desktop_files;
    int taskbar;
    pid_t window_manager;
    int window_manager_failed;
    guint x_source;
    guint items_source;
    guint monitor_source;
    guint wm_source;
} ShellState;

void shell_show_error(GtkWindow *parent, const char *message);
void shell_update_styles(ShellState *state);
void shell_update_wallpapers(ShellState *state);
void shell_publish_features(ShellState *state);
int shell_save_settings(ShellState *state);
void shell_refresh_desktop_windows(ShellState *state);
int shell_launch_action(ShellState *state, int action);
void desktop_view_center_window(ShellState *state, GtkWidget *window,
                                int width, int height);
void launcher_open(ShellState *state);
void launcher_close(ShellState *state);
void settings_open(ShellState *state);

#endif
