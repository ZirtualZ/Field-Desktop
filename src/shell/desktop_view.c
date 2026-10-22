#include "gtk_shell_internal.h"

#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>

#include <stdio.h>
#include <string.h>

void desktop_view_center_window(ShellState *state, GtkWidget *window,
                                int width, int height)
{
    gtk_widget_realize(window);
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (surface == NULL || !GDK_IS_X11_SURFACE(surface)) {
        fprintf(stderr, "Could not position the desktop dialog on X11.\n");
        return;
    }
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Window xid = gdk_x11_surface_get_xid(surface);
    G_GNUC_END_IGNORE_DEPRECATIONS
    MonitorGeometry monitor = state->monitor_count > 0
        ? state->monitors[0]
        : (MonitorGeometry){0, 0, DisplayWidth(state->xdisplay,
                                               DefaultScreen(state->xdisplay)),
                            DisplayHeight(state->xdisplay,
                                          DefaultScreen(state->xdisplay))};
    XMoveWindow(state->xdisplay, xid,
                monitor.x + (monitor.width - width) / 2,
                monitor.y + (monitor.height - height) / 2);
    XSync(state->xdisplay, False);
}

void shell_update_wallpapers(ShellState *state)
{
    GdkTexture *texture = NULL;
    if (state->appearance.image_enabled) {
        GError *error = NULL;
        texture = gdk_texture_new_from_filename(state->appearance.image_path,
                                                 &error);
        if (texture == NULL) {
            fprintf(stderr, "Could not load desktop wallpaper: %s\n",
                    error != NULL ? error->message : "unknown image error");
            g_clear_error(&error);
            return;
        }
    }
    for (size_t i = 0; i < state->monitor_count; ++i) {
        if (state->wallpaper_pictures[i] != NULL) {
            gtk_picture_set_paintable(GTK_PICTURE(state->wallpaper_pictures[i]),
                                      texture != NULL
                                          ? GDK_PAINTABLE(texture) : NULL);
        }
    }
    g_clear_object(&texture);
}

static gboolean desktop_key_pressed(GtkEventControllerKey *controller,
                                    guint keyval, guint keycode,
                                    GdkModifierType modifiers, gpointer data)
{
    ShellState *state = data;
    (void)controller;
    (void)keycode;
    (void)modifiers;
    if (keyval != GDK_KEY_Escape) {
        return GDK_EVENT_PROPAGATE;
    }
    if (state->launcher != NULL) {
        launcher_close(state);
    } else if (state->settings != NULL) {
        GtkWidget *settings = state->settings;
        state->settings = NULL;
        state->wallpaper_toggle = NULL;
        gtk_window_destroy(GTK_WINDOW(settings));
    } else {
        g_application_quit(G_APPLICATION(state->application));
    }
    return GDK_EVENT_STOP;
}

static void shortcut_clicked(GtkButton *button, gpointer data)
{
    ShellState *state = data;
    int action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "action"));
    if (!shell_launch_action(state, action)) {
        shell_show_error(GTK_WINDOW(state->desktop_windows[0]),
                         "The requested application could not be started.");
    }
}

static void desktop_item_clicked(GtkButton *button, gpointer data)
{
    ShellState *state = data;
    guint encoded = GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(button), "item-index"));
    const DesktopItem *item = desktop_items_get(&state->desktop_items,
                                                (size_t)(encoded - 1));
    if (item == NULL || !desktop_item_open(item)) {
        shell_show_error(GTK_WINDOW(state->desktop_windows[0]),
                         "This desktop item could not be opened.");
    }
}

static GtkWidget *make_shortcut(ShellState *state, int action,
                                const char *label, const char *icon)
{
    GtkWidget *button = gtk_button_new();
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *image = gtk_image_new_from_icon_name(icon);
    GtkWidget *text = gtk_label_new(label);
    gtk_image_set_pixel_size(GTK_IMAGE(image), 34);
    gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(button, "desktop-shortcut");
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_box_append(GTK_BOX(content), image);
    gtk_box_append(GTK_BOX(content), text);
    g_object_set_data(G_OBJECT(button), "action", GINT_TO_POINTER(action));
    g_signal_connect(button, "clicked", G_CALLBACK(shortcut_clicked), state);
    return button;
}

static GtkWidget *make_desktop_item(ShellState *state, size_t index)
{
    const DesktopItem *item = desktop_items_get(&state->desktop_items, index);
    if (item == NULL) {
        return NULL;
    }
    GtkWidget *button = gtk_button_new();
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *image = gtk_image_new_from_icon_name(
        item->is_directory ? "folder-symbolic" : "text-x-generic-symbolic");
    GtkWidget *text = gtk_label_new(item->name);
    gtk_image_set_pixel_size(GTK_IMAGE(image), 30);
    gtk_label_set_max_width_chars(GTK_LABEL(text), 18);
    gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class(button, "desktop-shortcut");
    gtk_button_set_child(GTK_BUTTON(button), content);
    gtk_box_append(GTK_BOX(content), image);
    gtk_box_append(GTK_BOX(content), text);
    g_object_set_data(G_OBJECT(button), "item-index",
                      GUINT_TO_POINTER((guint)index + 1));
    g_signal_connect(button, "clicked", G_CALLBACK(desktop_item_clicked), state);
    return button;
}

static void add_grid_button(GtkGrid *grid, GtkWidget *button,
                            int index, int columns)
{
    gtk_grid_attach(grid, button, index % columns, index / columns, 1, 1);
}

static GtkWidget *make_panel(ShellState *state, size_t monitor_index)
{
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(panel, "panel");
    GtkWidget *brand = gtk_label_new("FIELD");
    gtk_widget_add_css_class(brand, "brand");
    GtkWidget *apps = gtk_button_new_with_label("Applications");
    gtk_widget_add_css_class(apps, "primary-action");
    g_object_set_data(G_OBJECT(apps), "action",
                      GINT_TO_POINTER(SHELL_ACTION_APPS));
    g_signal_connect(apps, "clicked", G_CALLBACK(shortcut_clicked), state);
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    GtkWidget *display = gtk_label_new(NULL);
    char display_text[32];
    snprintf(display_text, sizeof(display_text), "Display %zu",
             monitor_index + 1);
    gtk_label_set_text(GTK_LABEL(display), display_text);
    gtk_widget_add_css_class(display, "muted");
    GtkWidget *settings = gtk_button_new_with_label("Settings");
    g_object_set_data(G_OBJECT(settings), "action",
                      GINT_TO_POINTER(SHELL_ACTION_SETTINGS));
    g_signal_connect(settings, "clicked", G_CALLBACK(shortcut_clicked), state);
    gtk_box_append(GTK_BOX(panel), brand);
    gtk_box_append(GTK_BOX(panel), apps);
    gtk_box_append(GTK_BOX(panel), spacer);
    gtk_box_append(GTK_BOX(panel), display);
    gtk_box_append(GTK_BOX(panel), settings);
    return panel;
}

static int set_desktop_window_type(ShellState *state, GtkWidget *window)
{
    gtk_widget_realize(window);
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (surface == NULL || !GDK_IS_X11_SURFACE(surface)) {
        fprintf(stderr, "Desktop surface is not backed by X11.\n");
        return 0;
    }
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Window xid = gdk_x11_surface_get_xid(surface);
    G_GNUC_END_IGNORE_DEPRECATIONS
    XChangeProperty(state->xdisplay, xid,
                    XInternAtom(state->xdisplay, "_NET_WM_WINDOW_TYPE", False),
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&state->desktop_type_atom, 1);
    XSync(state->xdisplay, False);
    return 1;
}

static GtkWidget *make_desktop_window(ShellState *state, size_t monitor_index)
{
    static const struct {
        const char *label;
        const char *icon;
        int action;
    } shortcuts[] = {
        {"Apps", "view-grid-symbolic", SHELL_ACTION_APPS},
        {"Terminal", "utilities-terminal-symbolic", SHELL_ACTION_TERMINAL},
        {"Files", "folder-symbolic", SHELL_ACTION_FILES},
        {"Browser", "web-browser-symbolic", SHELL_ACTION_BROWSER},
        {"Settings", "preferences-system-symbolic", SHELL_ACTION_SETTINGS}
    };

    GtkWidget *window = gtk_application_window_new(state->application);
    gtk_window_set_title(GTK_WINDOW(window), "Field Desktop");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window),
                                state->monitors[monitor_index].width,
                                state->monitors[monitor_index].height);

    GtkWidget *overlay = gtk_overlay_new();
    GtkWidget *surface = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(surface, "desktop-surface");
    GtkWidget *wallpaper = gtk_picture_new();
    gtk_picture_set_can_shrink(GTK_PICTURE(wallpaper), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(wallpaper), GTK_CONTENT_FIT_COVER);
    gtk_widget_set_hexpand(wallpaper, TRUE);
    gtk_widget_set_vexpand(wallpaper, TRUE);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), wallpaper);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), surface);
    gtk_window_set_child(GTK_WINDOW(window), overlay);
    state->wallpaper_pictures[monitor_index] = wallpaper;

    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed",
                     G_CALLBACK(desktop_key_pressed), state);
    gtk_widget_add_controller(window, keys);

    if (state->top_panel) {
        gtk_box_append(GTK_BOX(surface), make_panel(state, monitor_index));
    }

    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_margin_start(body, 24);
    gtk_widget_set_margin_top(body, 22);
    gtk_widget_set_margin_end(body, 24);
    gtk_widget_set_margin_bottom(body, 24);
    gtk_widget_set_vexpand(body, TRUE);
    gtk_box_append(GTK_BOX(surface), body);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_halign(grid, GTK_ALIGN_START);
    for (int i = 0; i < (int)(sizeof(shortcuts) / sizeof(shortcuts[0])); ++i) {
        GtkWidget *button = make_shortcut(state, shortcuts[i].action,
                                          shortcuts[i].label,
                                          shortcuts[i].icon);
        add_grid_button(GTK_GRID(grid), button, i, 6);
    }
    gtk_box_append(GTK_BOX(body), grid);

    if (state->desktop_files && state->desktop_items.count > 0) {
        GtkWidget *items = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(items), 8);
        gtk_grid_set_column_spacing(GTK_GRID(items), 8);
        gtk_widget_set_halign(items, GTK_ALIGN_START);
        for (size_t i = 0; i < state->desktop_items.count; ++i) {
            GtkWidget *button = make_desktop_item(state, i);
            if (button != NULL) {
                add_grid_button(GTK_GRID(items), button, (int)i, 6);
            }
        }
        GtkWidget *scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), items);
        gtk_widget_set_vexpand(scroll, TRUE);
        gtk_box_append(GTK_BOX(body), scroll);
    }

    if (!set_desktop_window_type(state, window)) {
        gtk_window_destroy(GTK_WINDOW(window));
        return NULL;
    }
    gtk_window_present(GTK_WINDOW(window));
    GdkSurface *native_surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (native_surface != NULL && GDK_IS_X11_SURFACE(native_surface)) {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        Window xid = gdk_x11_surface_get_xid(native_surface);
        G_GNUC_END_IGNORE_DEPRECATIONS
        MonitorGeometry monitor = state->monitors[monitor_index];
        XMoveResizeWindow(state->xdisplay, xid, monitor.x, monitor.y,
                          (unsigned int)monitor.width,
                          (unsigned int)monitor.height);
        XLowerWindow(state->xdisplay, xid);
        XFlush(state->xdisplay);
    }
    return window;
}

void shell_refresh_desktop_windows(ShellState *state)
{
    if (state->settings != NULL) {
        gtk_window_set_transient_for(GTK_WINDOW(state->settings), NULL);
    }
    if (state->launcher != NULL) {
        gtk_window_set_transient_for(GTK_WINDOW(state->launcher), NULL);
    }
    for (size_t i = 0; i < SHELL_MAX_MONITORS; ++i) {
        state->wallpaper_pictures[i] = NULL;
        if (state->desktop_windows[i] != NULL) {
            gtk_window_destroy(GTK_WINDOW(state->desktop_windows[i]));
            state->desktop_windows[i] = NULL;
        }
    }
    state->monitor_count = monitor_layout_get(
        state->xdisplay, DefaultRootWindow(state->xdisplay),
        state->monitors, SHELL_MAX_MONITORS);
    if (state->monitor_count == 0) {
        fprintf(stderr, "No active display monitors were detected.\n");
        return;
    }
    for (size_t i = 0; i < state->monitor_count; ++i) {
        state->desktop_windows[i] = make_desktop_window(state, i);
    }
    if (state->settings != NULL && state->desktop_windows[0] != NULL) {
        gtk_window_set_transient_for(
            GTK_WINDOW(state->settings),
            GTK_WINDOW(state->desktop_windows[0]));
    }
    if (state->launcher != NULL && state->desktop_windows[0] != NULL) {
        gtk_window_set_transient_for(
            GTK_WINDOW(state->launcher),
            GTK_WINDOW(state->desktop_windows[0]));
    }
    shell_update_wallpapers(state);
}
