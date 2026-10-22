#define _POSIX_C_SOURCE 200809L

#include "icon_themes.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int path_add_unique(GPtrArray *paths, GHashTable *seen,
                           const char *path)
{
    if (path == NULL || path[0] != '/' || !g_file_test(path, G_FILE_TEST_IS_DIR) ||
        g_hash_table_contains(seen, path)) {
        return 0;
    }
    char *copy = g_strdup(path);
    if (copy == NULL) {
        return 0;
    }
    g_ptr_array_add(paths, copy);
    g_hash_table_add(seen, g_strdup(path));
    return 1;
}

static char *theme_display_name(const char *path, const char *id)
{
    char *index_path = g_build_filename(path, id, "index.theme", NULL);
    GKeyFile *key_file = g_key_file_new();
    char *name = NULL;
    GError *error = NULL;
    if (g_key_file_load_from_file(key_file, index_path, G_KEY_FILE_NONE,
                                  &error)) {
        name = g_key_file_get_string(key_file, "Icon Theme", "Name", NULL);
    }
    g_clear_error(&error);
    g_key_file_unref(key_file);
    g_free(index_path);
    if (name == NULL || name[0] == '\0') {
        g_free(name);
        return g_strdup(id);
    }
    return name;
}

static void add_theme(ShellState *state, GHashTable *seen,
                      const char *directory, const char *id)
{
    if (id == NULL || id[0] == '\0' ||
        strlen(id) >= SHELL_ICON_THEME_ID_SIZE ||
        g_hash_table_contains(seen, id)) {
        return;
    }
    char *index_path = g_build_filename(directory, id, "index.theme", NULL);
    if (!g_file_test(index_path, G_FILE_TEST_IS_REGULAR)) {
        g_free(index_path);
        return;
    }
    g_free(index_path);

    ShellIconTheme *theme = g_new0(ShellIconTheme, 1);
    theme->id = g_strdup(id);
    theme->name = theme_display_name(directory, id);
    if (theme->id == NULL || theme->name == NULL) {
        g_free(theme->id);
        g_free(theme->name);
        g_free(theme);
        return;
    }
    g_ptr_array_add(state->icon_themes, theme);
    g_hash_table_add(seen, g_strdup(id));
}

static void scan_directory(ShellState *state, GHashTable *seen,
                           const char *directory)
{
    if (directory == NULL || !g_file_test(directory, G_FILE_TEST_IS_DIR)) {
        return;
    }
    GError *error = NULL;
    GDir *dir = g_dir_open(directory, 0, &error);
    if (dir == NULL) {
        fprintf(stderr, "Could not scan icon themes in %s: %s\n", directory,
                error != NULL ? error->message : "unknown directory error");
        g_clear_error(&error);
        return;
    }
    const char *entry;
    while ((entry = g_dir_read_name(dir)) != NULL) {
        add_theme(state, seen, directory, entry);
    }
    g_dir_close(dir);
}

static gint compare_theme_names(gconstpointer left, gconstpointer right)
{
    const ShellIconTheme *first = *(ShellIconTheme *const *)left;
    const ShellIconTheme *second = *(ShellIconTheme *const *)right;
    if (strcmp(first->id, "__system__") == 0) {
        return -1;
    }
    if (strcmp(second->id, "__system__") == 0) {
        return 1;
    }
    return g_utf8_collate(first->name, second->name);
}

static void apply_theme_name(GtkIconTheme *theme, const char *theme_id)
{
    gtk_icon_theme_set_theme_name(
        theme, strcmp(theme_id, "__system__") == 0 ? NULL : theme_id);
}

void icon_themes_initialize(ShellState *state)
{
    icon_themes_destroy(state);
    state->icon_themes = g_ptr_array_new_with_free_func(g_free);
    GdkDisplay *display = gdk_display_get_default();
    GtkIconTheme *theme = gtk_icon_theme_get_for_display(display);
    char **gtk_paths = gtk_icon_theme_get_search_path(theme);
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    GHashTable *seen_paths = g_hash_table_new_full(
        g_str_hash, g_str_equal, g_free, NULL);

    const char *home = g_get_home_dir();
    char *home_icons = g_build_filename(home, ".icons", NULL);
    char *data_icons = g_build_filename(
        g_get_user_data_dir(), "icons", NULL);
    path_add_unique(paths, seen_paths, home_icons);
    path_add_unique(paths, seen_paths, data_icons);
    for (char **path = gtk_paths; path != NULL && *path != NULL; ++path) {
        path_add_unique(paths, seen_paths, *path);
    }
    const char *const *data_dirs = g_get_system_data_dirs();
    for (size_t i = 0; data_dirs[i] != NULL; ++i) {
        char *icons = g_build_filename(data_dirs[i], "icons", NULL);
        path_add_unique(paths, seen_paths, icons);
        g_free(icons);
    }
    g_ptr_array_add(paths, NULL);
    gtk_icon_theme_set_search_path(theme,
                                   (const char *const *)paths->pdata);

    GHashTable *seen_themes = g_hash_table_new_full(
        g_str_hash, g_str_equal, g_free, NULL);
    for (guint i = 0; i + 1 < paths->len; ++i) {
        scan_directory(state, seen_themes, g_ptr_array_index(paths, i));
    }

    char *current_name = gtk_icon_theme_get_theme_name(theme);
    if (current_name == NULL || current_name[0] == '\0') {
        g_free(current_name);
        current_name = g_strdup("__system__");
    }
    if (!g_hash_table_contains(seen_themes, current_name)) {
        ShellIconTheme *system = g_new0(ShellIconTheme, 1);
        system->id = g_strdup(current_name);
        system->name = g_strdup(strcmp(current_name, "__system__") == 0
                                    ? "System default" : current_name);
        g_ptr_array_add(state->icon_themes, system);
    }
    g_ptr_array_sort(state->icon_themes, compare_theme_names);

    if (state->icon_theme_id[0] == '\0' ||
        !icon_themes_select(state, state->icon_theme_id)) {
        snprintf(state->icon_theme_id, sizeof(state->icon_theme_id), "%s",
                 current_name);
        apply_theme_name(theme, current_name);
    }

    g_free(current_name);
    g_hash_table_unref(seen_themes);
    g_hash_table_unref(seen_paths);
    g_ptr_array_unref(paths);
    g_strfreev(gtk_paths);
    g_free(home_icons);
    g_free(data_icons);
}

void icon_themes_destroy(ShellState *state)
{
    if (state->icon_themes != NULL) {
        for (guint i = 0; i < state->icon_themes->len; ++i) {
            ShellIconTheme *theme = g_ptr_array_index(state->icon_themes, i);
            g_free(theme->id);
            g_free(theme->name);
        }
        g_ptr_array_unref(state->icon_themes);
        state->icon_themes = NULL;
    }
}

int icon_themes_select(ShellState *state, const char *theme_id)
{
    if (state->icon_themes == NULL || theme_id == NULL) {
        return 0;
    }
    for (guint i = 0; i < state->icon_themes->len; ++i) {
        const ShellIconTheme *theme = g_ptr_array_index(state->icon_themes, i);
        if (strcmp(theme->id, theme_id) == 0) {
            snprintf(state->icon_theme_id, sizeof(state->icon_theme_id), "%s",
                     theme->id);
            apply_theme_name(gtk_icon_theme_get_for_display(
                                 gdk_display_get_default()), theme->id);
            return 1;
        }
    }
    return 0;
}
