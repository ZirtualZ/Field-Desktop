#define _POSIX_C_SOURCE 200809L

#include "desktop_items.h"

#include "applications.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

static int compare_items(const void *left, const void *right)
{
    const DesktopItem *first = left;
    const DesktopItem *second = right;
    if (first->is_directory != second->is_directory) {
        return second->is_directory - first->is_directory;
    }
    return strcasecmp(first->name, second->name);
}

int desktop_items_refresh(DesktopItems *desktop)
{
    if (desktop == NULL || desktop->items == NULL || desktop->directory[0] == '\0') {
        return 0;
    }
    DIR *directory = opendir(desktop->directory);
    if (directory == NULL) {
        if (errno != ENOENT) {
            perror("Could not read Desktop folder");
        } else {
            desktop->count = 0;
        }
        return errno == ENOENT;
    }

    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL && count < desktop->capacity) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char path[DESKTOP_ITEM_PATH_SIZE];
        int length = snprintf(path, sizeof(path), "%s/%s",
                              desktop->directory, entry->d_name);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            continue;
        }
        struct stat attributes;
        if (stat(path, &attributes) != 0) {
            continue;
        }
        DesktopItem *item = &desktop->items[count];
        snprintf(item->name, sizeof(item->name), "%s", entry->d_name);
        snprintf(item->path, sizeof(item->path), "%s", path);
        item->is_directory = S_ISDIR(attributes.st_mode);
        size_t name_length = strlen(entry->d_name);
        item->is_desktop_entry = name_length > 8 &&
            strcmp(entry->d_name + name_length - 8, ".desktop") == 0;
        ++count;
    }
    closedir(directory);
    qsort(desktop->items, count, sizeof(*desktop->items), compare_items);
    desktop->count = count;
    return 1;
}

int desktop_items_init(DesktopItems *desktop, size_t capacity)
{
    if (desktop == NULL || capacity == 0) {
        return 0;
    }
    memset(desktop, 0, sizeof(*desktop));
    desktop->watch_fd = -1;
    desktop->watch_id = -1;
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        fprintf(stderr, "Desktop folder unavailable: HOME is not set.\n");
        return 0;
    }
    int length = snprintf(desktop->directory, sizeof(desktop->directory),
                          "%s/Desktop", home);
    if (length < 0 || (size_t)length >= sizeof(desktop->directory)) {
        fprintf(stderr, "Desktop folder path is too long.\n");
        return 0;
    }
    desktop->items = calloc(capacity, sizeof(*desktop->items));
    if (desktop->items == NULL) {
        perror("Could not allocate Desktop folder items");
        return 0;
    }
    desktop->capacity = capacity;
    if (!desktop_items_refresh(desktop)) {
        free(desktop->items);
        memset(desktop, 0, sizeof(*desktop));
        desktop->watch_fd = -1;
        desktop->watch_id = -1;
        return 0;
    }

    desktop->watch_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (desktop->watch_fd >= 0) {
        desktop->watch_id = inotify_add_watch(
            desktop->watch_fd, desktop->directory,
            IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO |
            IN_CLOSE_WRITE | IN_ATTRIB);
        if (desktop->watch_id < 0) {
            close(desktop->watch_fd);
            desktop->watch_fd = -1;
        }
    }
    return 1;
}

void desktop_items_destroy(DesktopItems *desktop)
{
    if (desktop == NULL) {
        return;
    }
    if (desktop->watch_fd >= 0) {
        if (desktop->watch_id >= 0) {
            inotify_rm_watch(desktop->watch_fd, desktop->watch_id);
        }
        close(desktop->watch_fd);
    }
    free(desktop->items);
    memset(desktop, 0, sizeof(*desktop));
    desktop->watch_fd = -1;
    desktop->watch_id = -1;
}

int desktop_items_handle_changes(DesktopItems *desktop)
{
    if (desktop == NULL || desktop->watch_fd < 0) {
        return 0;
    }
    char events[4096];
    int changed = 0;
    for (;;) {
        ssize_t bytes = read(desktop->watch_fd, events, sizeof(events));
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            perror("Could not read Desktop folder changes");
            return 0;
        }
        if (bytes == 0) {
            break;
        }
        changed = 1;
    }
    return changed ? desktop_items_refresh(desktop) : 0;
}

const DesktopItem *desktop_items_get(const DesktopItems *desktop, size_t index)
{
    return desktop != NULL && index < desktop->count
        ? &desktop->items[index] : NULL;
}

int desktop_item_open(const DesktopItem *item)
{
    if (item == NULL || item->path[0] == '\0') {
        return 0;
    }
    if (item->is_desktop_entry) {
        Application application;
        if (!application_load_desktop_file(item->path, &application, 0)) {
            fprintf(stderr, "Desktop entry is not a launchable application: %s\n",
                    item->path);
            return 0;
        }
        return application_launch(&application);
    }
    return application_open_path(item->path);
}
