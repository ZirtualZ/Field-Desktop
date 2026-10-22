#define _POSIX_C_SOURCE 200809L

#include "animations.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int settings_path(char *path, size_t size)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return 0;
    }
    int length = snprintf(path, size,
                          "%s/.config/simple-desktop/animations", home);
    return length > 0 && (size_t)length < size;
}

void animations_init(DesktopAnimations *animations)
{
    animations->enabled = 0;
    char path[1200];
    if (!settings_path(path, sizeof(path))) {
        return;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    char value[16];
    animations->enabled = fgets(value, sizeof(value), file) != NULL &&
        (strcmp(value, "1\n") == 0 || strcmp(value, "1") == 0);
    fclose(file);
}

int animations_set_enabled(DesktopAnimations *animations, int enabled)
{
    char path[1200];
    if (!settings_path(path, sizeof(path))) {
        fprintf(stderr, "Cannot save animation setting without a home directory.\n");
        return 0;
    }
    char directory[1200];
    int length = snprintf(directory, sizeof(directory),
                          "%s/.config/simple-desktop",
                          getenv("HOME"));
    if (length < 0 || (size_t)length >= sizeof(directory)) {
        fprintf(stderr, "Animation settings path is too long.\n");
        return 0;
    }
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) {
        perror("Could not create animation settings directory");
        return 0;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        perror("Could not save animation setting");
        return 0;
    }
    int failed = fprintf(file, "%d\n", enabled != 0) < 0;
    if (fclose(file) != 0) {
        failed = 1;
    }
    if (failed) {
        perror("Could not save animation setting");
        return 0;
    }
    animations->enabled = enabled != 0;
    return 1;
}

void animations_map_window(Display *display, Window window, int x, int y,
                           unsigned int width, unsigned int height,
                           const DesktopAnimations *animations)
{
    if (animations == NULL || !animations->enabled) {
        XMoveResizeWindow(display, window, x, y, width, height);
        XMapRaised(display, window);
        return;
    }

    enum { FRAMES = 5 };
    int start_y = y - 12;
    XMoveResizeWindow(display, window, x, start_y, width, height);
    XMapRaised(display, window);
    for (int frame = 1; frame <= FRAMES; ++frame) {
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 12000000};
        int remaining;
        do {
            remaining = nanosleep(&delay, &delay);
        } while (remaining != 0 && errno == EINTR);
        int frame_y = start_y + ((y - start_y) * frame) / FRAMES;
        XMoveWindow(display, window, x, frame_y);
        XFlush(display);
    }
}
