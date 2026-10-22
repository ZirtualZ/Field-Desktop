#define _POSIX_C_SOURCE 200809L

#include "applications.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum { MAX_EXEC_ARGUMENTS = 64 };

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    return text;
}

static int executable_available(const char *name)
{
    if (name[0] == '\0') {
        return 0;
    }
    if (strchr(name, '/') != NULL) {
        return access(name, X_OK) == 0;
    }

    const char *path = getenv("PATH");
    if (path == NULL) {
        return 0;
    }
    char *copy = strdup(path);
    if (copy == NULL) {
        return 0;
    }

    int found = 0;
    char *save = NULL;
    for (char *directory = strtok_r(copy, ":", &save); directory != NULL;
         directory = strtok_r(NULL, ":", &save)) {
        char candidate[PATH_MAX];
        int length = snprintf(candidate, sizeof(candidate), "%s/%s",
                              directory[0] == '\0' ? "." : directory, name);
        if (length > 0 && (size_t)length < sizeof(candidate) &&
            access(candidate, X_OK) == 0) {
            found = 1;
            break;
        }
    }
    free(copy);
    return found;
}

static int desktop_entry_executable(const char *exec)
{
    char command[APPLICATION_EXEC_SIZE];
    size_t length = 0;
    int quoted = 0;
    int escaped = 0;

    for (const char *cursor = exec; *cursor != '\0'; ++cursor) {
        char character = *cursor;
        if (escaped) {
            if (length + 1 < sizeof(command)) {
                command[length++] = character;
            }
            escaped = 0;
        } else if (character == '\\') {
            escaped = 1;
        } else if (character == '"') {
            quoted = !quoted;
        } else if (!quoted && isspace((unsigned char)character)) {
            break;
        } else if (length + 1 < sizeof(command)) {
            command[length++] = character;
        }
    }
    command[length] = '\0';
    return !quoted && executable_available(command);
}

static int read_desktop_entry(const char *path, Application *application,
                              int allow_no_display)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }

    int in_entry = 0;
    int valid_type = 0;
    int hidden = 0;
    int no_display = 0;
    char *line = NULL;
    size_t line_capacity = 0;
    ssize_t line_length;
    while ((line_length = getline(&line, &line_capacity, file)) >= 0) {
        (void)line_length;
        char *text = trim(line);
        if (text[0] == '\0' || text[0] == '#' || text[0] == ';') {
            continue;
        }
        if (text[0] == '[') {
            in_entry = strcmp(text, "[Desktop Entry]") == 0;
            continue;
        }
        if (!in_entry) {
            continue;
        }

        char *equals = strchr(text, '=');
        if (equals == NULL) {
            continue;
        }
        *equals++ = '\0';
        char *key = trim(text);
        char *value = trim(equals);
        if (strcmp(key, "Type") == 0) {
            valid_type = strcmp(value, "Application") == 0;
        } else if (strcmp(key, "Name") == 0) {
            snprintf(application->name, sizeof(application->name), "%s", value);
        } else if (strcmp(key, "Exec") == 0) {
            snprintf(application->exec, sizeof(application->exec), "%s", value);
        } else if (strcmp(key, "TryExec") == 0 &&
                   !executable_available(value)) {
            hidden = 1;
        } else if (strcmp(key, "Hidden") == 0 && strcmp(value, "true") == 0) {
            hidden = 1;
        } else if (strcmp(key, "NoDisplay") == 0 &&
                   strcmp(value, "true") == 0) {
            no_display = 1;
        } else if (strcmp(key, "Terminal") == 0 &&
                   strcmp(value, "true") == 0) {
            hidden = 1;
        } else if (strcmp(key, "Categories") == 0) {
            snprintf(application->categories, sizeof(application->categories),
                     "%s", value);
        } else if (strcmp(key, "Icon") == 0) {
            snprintf(application->icon, sizeof(application->icon), "%s", value);
        }
    }
    free(line);
    fclose(file);

    if (!valid_type || hidden || (no_display && !allow_no_display) ||
        application->name[0] == '\0' ||
        application->exec[0] == '\0' ||
        !desktop_entry_executable(application->exec)) {
        return 0;
    }
    snprintf(application->path, sizeof(application->path), "%s", path);
    return 1;
}

int application_load_desktop_file(const char *path, Application *application,
                                  int allow_no_display)
{
    if (path == NULL || application == NULL) {
        return 0;
    }
    memset(application, 0, sizeof(*application));
    return read_desktop_entry(path, application, allow_no_display);
}

static int has_desktop_id(const Application *applications, size_t count,
                          const char *desktop_id)
{
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(applications[i].desktop_id, desktop_id) == 0) {
            return 1;
        }
    }
    return 0;
}

static void scan_applications_directory(const char *directory,
                                        Application *applications,
                                        size_t capacity, size_t *count)
{
    DIR *entries = opendir(directory);
    if (entries == NULL) {
        return;
    }

    struct dirent *entry;
    while (*count < capacity && (entry = readdir(entries)) != NULL) {
        size_t name_length = strlen(entry->d_name);
        if (name_length < 9 ||
            strcmp(entry->d_name + name_length - 8, ".desktop") != 0) {
            continue;
        }

        char path[PATH_MAX];
        int path_length = snprintf(path, sizeof(path), "%s/%s",
                                   directory, entry->d_name);
        if (path_length <= 0 || (size_t)path_length >= sizeof(path) ||
            has_desktop_id(applications, *count, entry->d_name)) {
            continue;
        }

        Application application = {0};
        if (!read_desktop_entry(path, &application, 0)) {
            continue;
        }
        snprintf(application.desktop_id, sizeof(application.desktop_id),
                 "%s", entry->d_name);
        applications[(*count)++] = application;
    }
    closedir(entries);
}

static int compare_applications(const void *left, const void *right)
{
    const Application *first = left;
    const Application *second = right;
    return strcasecmp(first->name, second->name);
}

size_t applications_load(Application *applications, size_t capacity)
{
    if (applications == NULL || capacity == 0) {
        return 0;
    }

    size_t count = 0;
    const char *data_home = getenv("XDG_DATA_HOME");
    char local_directory[PATH_MAX];
    if (data_home != NULL && data_home[0] != '\0') {
        int length = snprintf(local_directory, sizeof(local_directory),
                              "%s/applications", data_home);
        if (length < 0 || (size_t)length >= sizeof(local_directory)) {
            local_directory[0] = '\0';
        }
    } else {
        const char *home = getenv("HOME");
        if (home != NULL && home[0] != '\0') {
            int length = snprintf(local_directory, sizeof(local_directory),
                                  "%s/.local/share/applications", home);
            if (length < 0 || (size_t)length >= sizeof(local_directory)) {
                local_directory[0] = '\0';
            }
        } else {
            local_directory[0] = '\0';
        }
    }
    if (local_directory[0] != '\0') {
        scan_applications_directory(local_directory, applications, capacity, &count);
    }

    const char *data_directories = getenv("XDG_DATA_DIRS");
    if (data_directories == NULL || data_directories[0] == '\0') {
        data_directories = "/usr/local/share:/usr/share";
    }
    char *copy = strdup(data_directories);
    if (copy != NULL) {
        char *save = NULL;
        for (char *directory = strtok_r(copy, ":", &save);
             directory != NULL && count < capacity;
             directory = strtok_r(NULL, ":", &save)) {
            char applications_directory[PATH_MAX];
            int length = snprintf(applications_directory,
                                  sizeof(applications_directory),
                                  "%s/applications", directory);
            if (length > 0 && (size_t)length < sizeof(applications_directory)) {
                scan_applications_directory(applications_directory, applications,
                                            capacity, &count);
            }
        }
        free(copy);
    }
    qsort(applications, count, sizeof(*applications), compare_applications);
    return count;
}

static int contains_case_insensitive(const char *text, const char *query)
{
    if (query[0] == '\0') {
        return 1;
    }
    for (const char *start = text; *start != '\0'; ++start) {
        const char *left = start;
        const char *right = query;
        while (*left != '\0' && *right != '\0' &&
               tolower((unsigned char)*left) == tolower((unsigned char)*right)) {
            ++left;
            ++right;
        }
        if (*right == '\0') {
            return 1;
        }
    }
    return 0;
}

int application_matches(const Application *application, const char *query)
{
    return contains_case_insensitive(application->name, query) ||
           contains_case_insensitive(application->exec, query);
}

static void free_arguments(char **arguments, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        free(arguments[i]);
    }
}

static int append_argument(char **arguments, size_t *count,
                           const char *value, size_t length)
{
    if (*count >= MAX_EXEC_ARGUMENTS) {
        return 0;
    }
    char *argument = strndup(value, length);
    if (argument == NULL) {
        return 0;
    }
    arguments[(*count)++] = argument;
    return 1;
}

static int build_arguments(const Application *application, char **arguments,
                           size_t *argument_count)
{
    const char *cursor = application->exec;
    size_t count = 0;
    while (*cursor != '\0') {
        while (isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }

        char token[APPLICATION_EXEC_SIZE];
        size_t token_length = 0;
        int quoted = 0;
        int has_field_code = 0;
        while (*cursor != '\0' && (quoted || !isspace((unsigned char)*cursor))) {
            char character = *cursor++;
            if (character == '"') {
                quoted = !quoted;
            } else if (character == '\\' && *cursor != '\0') {
                if (token_length + 1 >= sizeof(token)) {
                    free_arguments(arguments, count);
                    return 0;
                }
                token[token_length++] = *cursor++;
            } else if (character == '%' && *cursor != '\0') {
                char code = *cursor++;
                if (code == '%' || code == 'c' || code == 'k') {
                    const char *replacement = code == '%' ? "%" :
                        (code == 'c' ? application->name : application->path);
                    size_t replacement_length = strlen(replacement);
                    if (replacement_length >= sizeof(token) - token_length) {
                        free_arguments(arguments, count);
                        return 0;
                    }
                    memcpy(token + token_length, replacement, replacement_length);
                    token_length += replacement_length;
                } else if (code == 'f' || code == 'F' || code == 'u' ||
                           code == 'U' || code == 'i') {
                    has_field_code = 1;
                } else {
                    fprintf(stderr, "Unsupported field code %%%c in %s\n",
                            code, application->path);
                    free_arguments(arguments, count);
                    return 0;
                }
            } else {
                if (token_length + 1 >= sizeof(token)) {
                    free_arguments(arguments, count);
                    return 0;
                }
                token[token_length++] = character;
            }
        }
        if (quoted) {
            free_arguments(arguments, count);
            return 0;
        }
        if (token_length > 0) {
            token[token_length] = '\0';
            if (!append_argument(arguments, &count, token, token_length)) {
                free_arguments(arguments, count);
                return 0;
            }
        } else if (has_field_code) {
            continue;
        }
    }

    if (count == 0) {
        return 0;
    }
    arguments[count] = NULL;
    *argument_count = count;
    return 1;
}

static int launch_arguments(char **arguments)
{
    pid_t child = fork();
    if (child == 0) {
        execvp(arguments[0], arguments);
        perror("Could not start application");
        _exit(127);
    }
    if (child < 0) {
        perror("Could not start application");
        return 0;
    }
    return 1;
}

int application_launch(const Application *application)
{
    char *arguments[MAX_EXEC_ARGUMENTS + 1] = {0};
    size_t argument_count = 0;
    if (!build_arguments(application, arguments, &argument_count)) {
        fprintf(stderr, "Invalid Exec entry in %s\n", application->path);
        return 0;
    }

    int launched = launch_arguments(arguments);
    free_arguments(arguments, argument_count);
    return launched;
}

int application_open_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        fprintf(stderr, "Cannot open an empty path.\n");
        return 0;
    }
    if (!executable_available("xdg-open")) {
        fprintf(stderr, "Could not open %s: install xdg-utils for default-app support.\n",
                path);
        return 0;
    }
    char *arguments[] = {"xdg-open", (char *)path, NULL};
    return launch_arguments(arguments);
}

static int has_category(const Application *application, const char *category)
{
    size_t category_length = strlen(category);
    const char *cursor = application->categories;
    while (*cursor != '\0') {
        const char *end = strchr(cursor, ';');
        size_t length = end == NULL ? strlen(cursor) : (size_t)(end - cursor);
        if (length == category_length &&
            strncmp(cursor, category, category_length) == 0) {
            return 1;
        }
        if (end == NULL) {
            break;
        }
        cursor = end + 1;
    }
    return 0;
}

static int known_kind_executable(const Application *application,
                                 ApplicationKind kind)
{
    static const char *const browsers[] = {
        "firefox", "firefox-esr", "chromium", "chromium-browser",
        "google-chrome", "brave", "librewolf", "epiphany"
    };
    static const char *const file_managers[] = {
        "thunar", "pcmanfm", "pcmanfm-qt", "nemo", "dolphin",
        "nautilus", "caja", "xdg-open"
    };
    const char *const *names = kind == APPLICATION_BROWSER
        ? browsers : file_managers;
    size_t count = kind == APPLICATION_BROWSER
        ? sizeof(browsers) / sizeof(browsers[0])
        : sizeof(file_managers) / sizeof(file_managers[0]);

    for (size_t i = 0; i < count; ++i) {
        if (strstr(application->exec, names[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

static int launch_fallback(ApplicationKind kind)
{
    static const char *const browsers[] = {
        "firefox", "firefox-esr", "chromium", "chromium-browser",
        "google-chrome", "brave", "librewolf", "epiphany"
    };
    static const char *const file_managers[] = {
        "xdg-open", "thunar", "pcmanfm", "pcmanfm-qt", "nemo",
        "dolphin", "nautilus", "caja"
    };
    const char *const *names = kind == APPLICATION_BROWSER
        ? browsers : file_managers;
    size_t count = kind == APPLICATION_BROWSER
        ? sizeof(browsers) / sizeof(browsers[0])
        : sizeof(file_managers) / sizeof(file_managers[0]);

    for (size_t i = 0; i < count; ++i) {
        if (!executable_available(names[i])) {
            continue;
        }
        char *arguments[3] = {(char *)names[i], NULL, NULL};
        if (kind == APPLICATION_FILE_MANAGER &&
            (strcmp(names[i], "xdg-open") == 0 ||
             strcmp(names[i], "thunar") == 0 ||
             strcmp(names[i], "pcmanfm") == 0 ||
             strcmp(names[i], "pcmanfm-qt") == 0 ||
             strcmp(names[i], "nemo") == 0 ||
             strcmp(names[i], "dolphin") == 0 ||
             strcmp(names[i], "nautilus") == 0 ||
             strcmp(names[i], "caja") == 0)) {
            const char *home = getenv("HOME");
            arguments[1] = (char *)(home != NULL && home[0] != '\0' ? home : "/");
        }
        return launch_arguments(arguments);
    }
    fprintf(stderr, "No compatible %s application is installed.\n",
            kind == APPLICATION_BROWSER ? "browser" : "file manager");
    return 0;
}

int applications_launch_kind(const Application *applications, size_t count,
                             ApplicationKind kind)
{
    const char *category = kind == APPLICATION_BROWSER
        ? "WebBrowser" : "FileManager";
    for (size_t i = 0; i < count; ++i) {
        if (has_category(&applications[i], category)) {
            return application_launch(&applications[i]);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (known_kind_executable(&applications[i], kind)) {
            return application_launch(&applications[i]);
        }
    }
    return launch_fallback(kind);
}
