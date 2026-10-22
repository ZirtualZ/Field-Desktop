#ifndef FIELD_DESKTOP_STARTUP_APPS_H
#define FIELD_DESKTOP_STARTUP_APPS_H

#include "applications.h"

#include <stddef.h>

typedef struct {
    Application *applications;
    size_t count;
    size_t capacity;
    int enabled;
} StartupApps;

int startup_apps_init(StartupApps *startup, size_t capacity);
void startup_apps_destroy(StartupApps *startup);
int startup_apps_set_enabled(StartupApps *startup, int enabled);
int startup_apps_launch_enabled(const StartupApps *startup);

#endif
