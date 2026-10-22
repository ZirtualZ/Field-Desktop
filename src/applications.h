#ifndef FIELD_DESKTOP_APPLICATIONS_H
#define FIELD_DESKTOP_APPLICATIONS_H

#include <stddef.h>

enum {
    APPLICATION_NAME_SIZE = 128,
    APPLICATION_EXEC_SIZE = 1024,
    APPLICATION_PATH_SIZE = 4096,
    APPLICATION_ID_SIZE = 256
};

typedef struct {
    char name[APPLICATION_NAME_SIZE];
    char exec[APPLICATION_EXEC_SIZE];
    char path[APPLICATION_PATH_SIZE];
    char desktop_id[APPLICATION_ID_SIZE];
    char categories[256];
    char icon[128];
} Application;

typedef enum {
    APPLICATION_BROWSER,
    APPLICATION_FILE_MANAGER
} ApplicationKind;

size_t applications_load(Application *applications, size_t capacity);
int application_load_desktop_file(const char *path, Application *application,
                                  int allow_no_display);
int application_matches(const Application *application, const char *query);
int application_launch(const Application *application);
int application_open_path(const char *path);
int applications_launch_kind(const Application *applications, size_t count,
                             ApplicationKind kind);

#endif
