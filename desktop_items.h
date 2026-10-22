#ifndef FIELD_DESKTOP_DESKTOP_ITEMS_H
#define FIELD_DESKTOP_DESKTOP_ITEMS_H

#include <stddef.h>

enum {
    DESKTOP_ITEM_NAME_SIZE = 256,
    DESKTOP_ITEM_PATH_SIZE = 4096
};

typedef struct {
    char name[DESKTOP_ITEM_NAME_SIZE];
    char path[DESKTOP_ITEM_PATH_SIZE];
    int is_directory;
    int is_desktop_entry;
} DesktopItem;

typedef struct {
    DesktopItem *items;
    size_t count;
    size_t capacity;
    char directory[DESKTOP_ITEM_PATH_SIZE];
    int watch_fd;
    int watch_id;
} DesktopItems;

int desktop_items_init(DesktopItems *desktop, size_t capacity);
void desktop_items_destroy(DesktopItems *desktop);
int desktop_items_refresh(DesktopItems *desktop);
int desktop_items_handle_changes(DesktopItems *desktop);
const DesktopItem *desktop_items_get(const DesktopItems *desktop, size_t index);
int desktop_item_open(const DesktopItem *item);

#endif
