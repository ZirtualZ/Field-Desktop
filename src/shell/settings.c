#include "gtk_shell_internal.h"

static gboolean window_closed(GtkWindow *window, gpointer data)
{
    ShellState *state = data;
    if (state->settings == GTK_WIDGET(window)) {
        state->settings = NULL;
        state->wallpaper_toggle = NULL;
    }
    return GDK_EVENT_PROPAGATE;
}

static gboolean key_pressed(GtkEventControllerKey *controller,
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
    GtkWidget *window = state->settings;
    state->settings = NULL;
    state->wallpaper_toggle = NULL;
    if (window != NULL) {
        gtk_window_destroy(GTK_WINDOW(window));
    }
    return GDK_EVENT_STOP;
}

static void option_toggled(GtkCheckButton *button, gpointer data)
{
    ShellState *state = data;
    const char *option = g_object_get_data(G_OBJECT(button), "option");
    int enabled = gtk_check_button_get_active(button);
    if (strcmp(option, "panel") == 0) {
        state->top_panel = enabled;
        shell_refresh_desktop_windows(state);
    } else if (strcmp(option, "files") == 0) {
        state->desktop_files = enabled;
        shell_refresh_desktop_windows(state);
    } else if (strcmp(option, "taskbar") == 0) {
        state->taskbar = enabled;
    }
    if (!shell_save_settings(state)) {
        shell_show_error(GTK_WINDOW(state->settings),
                         "The desktop preference could not be saved.");
    }
}

static void theme_changed(GObject *object, GParamSpec *parameter, gpointer data)
{
    ShellState *state = data;
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected == GTK_INVALID_LIST_POSITION ||
        !appearance_select_theme(&state->appearance, (int)selected)) {
        shell_show_error(GTK_WINDOW(state->settings),
                         "The appearance setting could not be saved.");
        return;
    }
    shell_update_styles(state);
    (void)parameter;
}

static void background_changed(GObject *object, GParamSpec *parameter,
                               gpointer data)
{
    ShellState *state = data;
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
    if (selected == GTK_INVALID_LIST_POSITION ||
        !appearance_select_background(&state->appearance, (int)selected)) {
        shell_show_error(GTK_WINDOW(state->settings),
                         "The background setting could not be saved.");
        return;
    }
    shell_update_styles(state);
    shell_update_wallpapers(state);
    if (state->wallpaper_toggle != NULL) {
        gtk_check_button_set_active(GTK_CHECK_BUTTON(state->wallpaper_toggle),
                                    FALSE);
    }
    (void)parameter;
}

static void wallpaper_toggled(GtkCheckButton *button, gpointer data)
{
    ShellState *state = data;
    int enabled = gtk_check_button_get_active(button);
    if (enabled && state->appearance.image_path[0] == '\0') {
        gtk_check_button_set_active(button, FALSE);
        shell_show_error(GTK_WINDOW(state->settings),
                         "Choose a wallpaper image before enabling it.");
        return;
    }
    int previous_enabled = state->appearance.image_enabled;
    state->appearance.image_enabled = enabled;
    if (!appearance_save(&state->appearance)) {
        state->appearance.image_enabled = previous_enabled;
        gtk_check_button_set_active(button, !enabled);
        shell_show_error(GTK_WINDOW(state->settings),
                         "The wallpaper setting could not be saved.");
        return;
    }
    shell_update_wallpapers(state);
}

static void wallpaper_selected(GObject *source, GAsyncResult *result,
                               gpointer data)
{
    ShellState *state = data;
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result,
                                              &error);
    if (file != NULL) {
        char *path = g_file_get_path(file);
        if (path == NULL ||
            !appearance_select_image(&state->appearance, path)) {
            shell_show_error(GTK_WINDOW(state->settings),
                             "The selected wallpaper could not be saved.");
        } else {
            state->appearance.image_enabled = 1;
            if (!appearance_save(&state->appearance)) {
                shell_show_error(GTK_WINDOW(state->settings),
                                 "The wallpaper could not be enabled.");
            } else {
                shell_update_wallpapers(state);
                if (state->wallpaper_toggle != NULL) {
                    gtk_check_button_set_active(
                        GTK_CHECK_BUTTON(state->wallpaper_toggle), TRUE);
                }
            }
        }
        g_free(path);
        g_clear_object(&file);
    } else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        shell_show_error(GTK_WINDOW(state->settings),
                         error != NULL ? error->message :
                         "The wallpaper picker could not be opened.");
    }
    g_clear_error(&error);
}

static void choose_wallpaper(GtkButton *button, gpointer data)
{
    ShellState *state = data;
    GtkFileDialog *chooser = gtk_file_dialog_new();
    gtk_file_dialog_set_title(chooser, "Choose desktop wallpaper");
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Images");
    gtk_file_filter_add_mime_type(filter, "image/*");
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(chooser, G_LIST_MODEL(filters));
    gtk_file_dialog_open(chooser, GTK_WINDOW(state->settings), NULL,
                         wallpaper_selected, state);
    g_object_unref(filters);
    g_object_unref(filter);
    g_object_unref(chooser);
    (void)button;
}

static GtkWidget *make_appearance_page(ShellState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(page, 20);
    gtk_widget_set_margin_end(page, 20);
    gtk_widget_set_margin_top(page, 18);
    gtk_widget_set_margin_bottom(page, 18);
    GtkWidget *heading = gtk_label_new("Appearance");
    gtk_widget_add_css_class(heading, "title-2");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(page), heading);

    GtkWidget *theme_label = gtk_label_new("Color theme");
    gtk_label_set_xalign(GTK_LABEL(theme_label), 0.0f);
    const char *theme_names[5] = {NULL};
    for (size_t i = 0; i < appearance_theme_count(); ++i) {
        const AppearanceTheme *theme = appearance_theme_at(i);
        if (theme != NULL) {
            theme_names[i] = theme->name;
        }
    }
    GtkWidget *themes = gtk_drop_down_new_from_strings(theme_names);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(themes), state->appearance.theme);
    g_signal_connect(themes, "notify::selected",
                     G_CALLBACK(theme_changed), state);

    GtkWidget *background_label = gtk_label_new("Desktop background");
    gtk_label_set_xalign(GTK_LABEL(background_label), 0.0f);
    const char *background_names[5] = {NULL};
    for (size_t i = 0; i < appearance_background_count(); ++i) {
        background_names[i] = appearance_background_name((int)i);
    }
    GtkWidget *backgrounds = gtk_drop_down_new_from_strings(background_names);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(backgrounds),
                               state->appearance.background);
    g_signal_connect(backgrounds, "notify::selected",
                     G_CALLBACK(background_changed), state);

    state->wallpaper_toggle = gtk_check_button_new_with_label(
        "Use the selected image as wallpaper");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(state->wallpaper_toggle),
                                state->appearance.image_enabled);
    g_signal_connect(state->wallpaper_toggle, "toggled",
                     G_CALLBACK(wallpaper_toggled), state);
    GtkWidget *choose = gtk_button_new_with_label("Choose wallpaper image...");
    g_signal_connect(choose, "clicked", G_CALLBACK(choose_wallpaper), state);

    gtk_box_append(GTK_BOX(page), theme_label);
    gtk_box_append(GTK_BOX(page), themes);
    gtk_box_append(GTK_BOX(page), background_label);
    gtk_box_append(GTK_BOX(page), backgrounds);
    gtk_box_append(GTK_BOX(page), state->wallpaper_toggle);
    gtk_box_append(GTK_BOX(page), choose);
    return page;
}

static GtkWidget *make_desktop_page(ShellState *state)
{
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(page, 20);
    gtk_widget_set_margin_end(page, 20);
    gtk_widget_set_margin_top(page, 18);
    gtk_widget_set_margin_bottom(page, 18);
    GtkWidget *heading = gtk_label_new("Desktop");
    gtk_widget_add_css_class(heading, "title-2");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    gtk_box_append(GTK_BOX(page), heading);
    const struct {
        const char *label;
        const char *option;
        int active;
    } options[] = {
        {"Show the top panel", "panel", state->top_panel},
        {"Show Desktop folder items", "files", state->desktop_files},
        {"Show the window-manager taskbar", "taskbar", state->taskbar}
    };
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
        GtkWidget *check = gtk_check_button_new_with_label(options[i].label);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(check), options[i].active);
        g_object_set_data(G_OBJECT(check), "option",
                          (gpointer)options[i].option);
        g_signal_connect(check, "toggled",
                         G_CALLBACK(option_toggled), state);
        gtk_box_append(GTK_BOX(page), check);
    }
    return page;
}

void settings_open(ShellState *state)
{
    if (state->settings != NULL) {
        gtk_window_present(GTK_WINDOW(state->settings));
        return;
    }
    GtkWidget *window = gtk_application_window_new(state->application);
    state->settings = window;
    gtk_window_set_title(GTK_WINDOW(window), "Field Desktop Settings");
    gtk_window_set_default_size(GTK_WINDOW(window), 580, 480);
    if (state->monitor_count > 0 && state->desktop_windows[0] != NULL) {
        gtk_window_set_transient_for(
            GTK_WINDOW(window), GTK_WINDOW(state->desktop_windows[0]));
    }
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);
    gtk_widget_set_margin_top(box, 18);
    gtk_widget_set_margin_bottom(box, 18);
    GtkWidget *heading = gtk_label_new("Settings");
    gtk_widget_add_css_class(heading, "title-1");
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    GtkWidget *notebook = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             make_appearance_page(state),
                             gtk_label_new("Appearance"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             make_desktop_page(state),
                             gtk_label_new("Desktop"));
    gtk_box_append(GTK_BOX(box), heading);
    gtk_box_append(GTK_BOX(box), notebook);
    gtk_window_set_child(GTK_WINDOW(window), box);
    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), state);
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(window, keys);
    g_signal_connect(window, "close-request",
                     G_CALLBACK(window_closed), state);
    desktop_view_center_window(state, window, 580, 480);
    gtk_window_present(GTK_WINDOW(window));
}
