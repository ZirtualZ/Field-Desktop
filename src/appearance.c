#define _POSIX_C_SOURCE 200809L

#include "appearance.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const AppearanceTheme themes[] = {
    {"Moss", "#142b22", "#e8fff0", "#72e6a2", "#204a37"},
    {"Ocean", "#10243a", "#e8f5ff", "#55c8ff", "#153f64"},
    {"Dusk", "#2e1838", "#fff0fb", "#ff88c9", "#54284f"},
    {"Sand", "#392513", "#fff3d6", "#ffd166", "#61401d"}
};

static const char *const backgrounds[] = {
    "Theme default", "Deep green", "Night blue", "Charcoal"
};

static const char *const background_colors[] = {
    NULL, "#15201c", "#101923", "#181818"
};

static const size_t theme_count = sizeof(themes) / sizeof(themes[0]);
static const size_t background_count =
    sizeof(backgrounds) / sizeof(backgrounds[0]);

static int config_path(char *path, size_t size)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return 0;
    }
    int length = snprintf(path, size,
                          "%s/.config/simple-desktop/appearance", home);
    return length > 0 && (size_t)length < size;
}

void appearance_init(Appearance *appearance)
{
    memset(appearance, 0, sizeof(*appearance));
    char path[1200];
    if (!config_path(path, sizeof(path))) {
        return;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    int theme = 0;
    int background = 0;
    int enabled = 0;
    char image[sizeof(appearance->image_path)] = "";
    if (fscanf(file, "%d\n%d\n%d\n", &theme, &background, &enabled) == 3) {
        if (theme >= 0 && (size_t)theme < theme_count) {
            appearance->theme = theme;
        }
        if (background >= 0 && (size_t)background < background_count) {
            appearance->background = background;
        }
        if (fgets(image, sizeof(image), file) != NULL) {
            image[strcspn(image, "\r\n")] = '\0';
            snprintf(appearance->image_path, sizeof(appearance->image_path),
                     "%s", image);
            appearance->image_enabled = enabled != 0 &&
                                        appearance->image_path[0] != '\0';
        }
    }
    fclose(file);
}

int appearance_save(const Appearance *appearance)
{
    char path[1200];
    if (!config_path(path, sizeof(path))) {
        fprintf(stderr, "Cannot save appearance without a home directory.\n");
        return 0;
    }
    const char *home = getenv("HOME");
    char config_directory[1200];
    int config_length = snprintf(config_directory, sizeof(config_directory),
                                 "%s/.config", home);
    if (config_length < 0 || (size_t)config_length >= sizeof(config_directory) ||
        (mkdir(config_directory, 0700) != 0 && errno != EEXIST)) {
        perror("Could not create user configuration directory");
        return 0;
    }
    char directory[1200];
    int directory_length = snprintf(directory, sizeof(directory),
                                    "%s/.config/simple-desktop", home);
    if (directory_length < 0 || (size_t)directory_length >= sizeof(directory) ||
        (mkdir(directory, 0700) != 0 && errno != EEXIST)) {
        perror("Could not create appearance settings directory");
        return 0;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        perror("Could not save appearance settings");
        return 0;
    }
    int failed = fprintf(file, "%d\n%d\n%d\n%s\n", appearance->theme,
                         appearance->background, appearance->image_enabled,
                         appearance->image_path) < 0;
    if (fclose(file) != 0) {
        failed = 1;
    }
    if (failed) {
        perror("Could not write appearance settings");
        return 0;
    }
    return 1;
}

size_t appearance_theme_count(void)
{
    return theme_count;
}

const AppearanceTheme *appearance_theme_at(size_t index)
{
    return index < theme_count ? &themes[index] : NULL;
}

size_t appearance_background_count(void)
{
    return background_count;
}

const char *appearance_background_name(int index)
{
    return index >= 0 && (size_t)index < background_count
        ? backgrounds[index] : NULL;
}

const char *appearance_background_color(int index)
{
    return index >= 0 && (size_t)index < background_count
        ? background_colors[index] : NULL;
}

int appearance_select_theme(Appearance *appearance, int index)
{
    if (appearance == NULL || index < 0 || (size_t)index >= theme_count) {
        return 0;
    }
    Appearance updated = *appearance;
    updated.theme = index;
    if (!appearance_save(&updated)) {
        return 0;
    }
    *appearance = updated;
    return 1;
}

int appearance_select_background(Appearance *appearance, int index)
{
    if (appearance == NULL || index < 0 ||
        (size_t)index >= background_count) {
        return 0;
    }
    Appearance updated = *appearance;
    updated.background = index;
    updated.image_enabled = 0;
    if (!appearance_save(&updated)) {
        return 0;
    }
    *appearance = updated;
    return 1;
}

int appearance_select_image(Appearance *appearance, const char *path)
{
    if (appearance == NULL || path == NULL || path[0] != '/') {
        fprintf(stderr, "Wallpaper path must be absolute.\n");
        return 0;
    }
    if (access(path, R_OK) != 0) {
        perror("Wallpaper image is not readable");
        return 0;
    }
    if (strlen(path) >= sizeof(appearance->image_path)) {
        fprintf(stderr, "Wallpaper image path is too long.\n");
        return 0;
    }
    Appearance updated = *appearance;
    snprintf(updated.image_path, sizeof(updated.image_path), "%s", path);
    if (!appearance_save(&updated)) {
        return 0;
    }
    *appearance = updated;
    return 1;
}
