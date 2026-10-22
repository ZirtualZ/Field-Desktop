#define _POSIX_C_SOURCE 200809L

#include "startup_apps.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int read_enabled_setting(void)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return 0;
    }
    char path[1200];
    int length = snprintf(path, sizeof(path),
                          "%s/.config/simple-desktop/startup-apps", home);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return 0;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }
    char value[16];
    int enabled = fgets(value, sizeof(value), file) != NULL &&
                  (strcmp(value, "1\n") == 0 || strcmp(value, "1") == 0);
    fclose(file);
    return enabled;
}

static int save_enabled_setting(int enabled)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        fprintf(stderr, "Cannot save startup setting without a home directory.\n");
        return 0;
    }
    char directory[1200];
    int length = snprintf(directory, sizeof(directory),
                          "%s/.config/simple-desktop", home);
    if (length < 0 || (size_t)length >= sizeof(directory)) {
        fprintf(stderr, "Startup settings path is too long.\n");
        return 0;
    }
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) {
        perror("Could not create startup settings directory");
        return 0;
    }
    char path[1200];
    length = snprintf(path, sizeof(path), "%s/startup-apps", directory);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        fprintf(stderr, "Startup settings path is too long.\n");
        return 0;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        perror("Could not save startup setting");
        return 0;
    }
    int failed = fprintf(file, "%d\n", enabled != 0) < 0;
    if (fclose(file) != 0) {
        failed = 1;
    }
    if (failed) {
        perror("Could not save startup setting");
        return 0;
    }
    return 1;
}

static int desktop_list_contains(const char *list, const char *desktop_name)
{
    size_t name_length = strlen(desktop_name);
    const char *cursor = list;
    while (*cursor != '\0') {
        while (*cursor == ';') {
            ++cursor;
        }
        const char *end = strchr(cursor, ';');
        size_t length = end == NULL ? strlen(cursor) :
                                      (size_t)(end - cursor);
        if (length == name_length &&
            strncmp(cursor, desktop_name, name_length) == 0) {
            return 1;
        }
        if (end == NULL) {
            break;
        }
        cursor = end + 1;
    }
    return 0;
}

static int autostart_entry_allowed(const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }
    int in_entry = 0;
    int allowed = 1;
    char *line = NULL;
    size_t capacity = 0;
    ssize_t line_length;
    while ((line_length = getline(&line, &capacity, file)) >= 0) {
        (void)line_length;
        if (line[0] == '[') {
            in_entry = strcmp(line, "[Desktop Entry]\n") == 0 ||
                       strcmp(line, "[Desktop Entry]") == 0;
            continue;
        }
        if (!in_entry) {
            continue;
        }
        char *equals = strchr(line, '=');
        if (equals == NULL) {
            continue;
        }
        *equals++ = '\0';
        equals[strcspn(equals, "\r\n")] = '\0';
        if (strcmp(line, "Hidden") == 0 && strcmp(equals, "true") == 0) {
            allowed = 0;
        } else if (strcmp(line, "X-GNOME-Autostart-enabled") == 0 &&
                   strcmp(equals, "false") == 0) {
            allowed = 0;
        } else if (strcmp(line, "OnlyShowIn") == 0 &&
                   *equals != '\0' &&
                   !desktop_list_contains(equals, "FieldDesktop")) {
            allowed = 0;
        } else if (strcmp(line, "NotShowIn") == 0 &&
                   desktop_list_contains(equals, "FieldDesktop")) {
            allowed = 0;
        }
    }
    free(line);
    fclose(file);
    return allowed;
}

static void load_directory(StartupApps *startup, const char *directory)
{
    DIR *entries = opendir(directory);
    if (entries == NULL) {
        return;
    }
    struct dirent *entry;
    while (startup->count < startup->capacity &&
           (entry = readdir(entries)) != NULL) {
        size_t name_length = strlen(entry->d_name);
        if (name_length < 9 ||
            strcmp(entry->d_name + name_length - 8, ".desktop") != 0) {
            continue;
        }
        char path[4096];
        int length = snprintf(path, sizeof(path), "%s/%s",
                              directory, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path) ||
            !autostart_entry_allowed(path)) {
            continue;
        }
        Application application;
        if (application_load_desktop_file(path, &application, 1)) {
            startup->applications[startup->count++] = application;
        }
    }
    closedir(entries);
}

int startup_apps_init(StartupApps *startup, size_t capacity)
{
    if (startup == NULL || capacity == 0) {
        return 0;
    }
    memset(startup, 0, sizeof(*startup));
    startup->applications = calloc(capacity, sizeof(*startup->applications));
    if (startup->applications == NULL) {
        perror("Could not allocate startup app list");
        return 0;
    }
    startup->capacity = capacity;
    startup->enabled = read_enabled_setting();

    const char *config_home = getenv("XDG_CONFIG_HOME");
    char directory[4096];
    if (config_home != NULL && config_home[0] != '\0') {
        int length = snprintf(directory, sizeof(directory),
                              "%s/autostart", config_home);
        if (length > 0 && (size_t)length < sizeof(directory)) {
            load_directory(startup, directory);
        }
    } else {
        const char *home = getenv("HOME");
        if (home != NULL && home[0] != '\0') {
            int length = snprintf(directory, sizeof(directory),
                                  "%s/.config/autostart", home);
            if (length > 0 && (size_t)length < sizeof(directory)) {
                load_directory(startup, directory);
            }
        }
    }
    return 1;
}

void startup_apps_destroy(StartupApps *startup)
{
    if (startup == NULL) {
        return;
    }
    free(startup->applications);
    memset(startup, 0, sizeof(*startup));
}

int startup_apps_set_enabled(StartupApps *startup, int enabled)
{
    if (startup == NULL || !save_enabled_setting(enabled)) {
        return 0;
    }
    startup->enabled = enabled != 0;
    return 1;
}

int startup_apps_launch_enabled(const StartupApps *startup)
{
    if (startup == NULL || !startup->enabled) {
        return 0;
    }
    int launched = 0;
    for (size_t i = 0; i < startup->count; ++i) {
        launched += application_launch(&startup->applications[i]);
    }
    return launched;
}
