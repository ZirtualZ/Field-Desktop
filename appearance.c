#define _POSIX_C_SOURCE 200809L

#include "appearance.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <dirent.h>
#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

static const AppearanceTheme themes[] = {
    {"Moss", "#142b22", "#e8fff0", "#72e6a2", "#204a37", "#1c7048"},
    {"Ocean", "#10243a", "#e8f5ff", "#55c8ff", "#153f64", "#126e9a"},
    {"Dusk", "#2e1838", "#fff0fb", "#ff88c9", "#54284f", "#883d78"},
    {"Sand", "#392513", "#fff3d6", "#ffd166", "#61401d", "#956222"}
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

static int image_extension(const char *name)
{
    const char *extension = strrchr(name, '.');
    if (extension == NULL) {
        return 0;
    }
    return strcasecmp(extension, ".png") == 0 ||
           strcasecmp(extension, ".jpg") == 0 ||
           strcasecmp(extension, ".jpeg") == 0 ||
           strcasecmp(extension, ".gif") == 0 ||
           strcasecmp(extension, ".bmp") == 0 ||
           strcasecmp(extension, ".webp") == 0;
}

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
    char image[sizeof(appearance->image_path)];
    image[0] = '\0';
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
        perror("Could not save appearance settings");
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
    if (index < 0 || (size_t)index >= theme_count) {
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
    if (index < 0 || (size_t)index >= background_count) {
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
    if (path == NULL || path[0] != '/') {
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

size_t appearance_images_load(char paths[][4096], size_t capacity)
{
    if (paths == NULL || capacity == 0) {
        return 0;
    }
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return 0;
    }
    const char *folders[] = {"Pictures", "Desktop"};
    size_t count = 0;
    for (size_t folder = 0; folder < sizeof(folders) / sizeof(folders[0]) &&
         count < capacity; ++folder) {
        char directory_path[4096];
        int length = snprintf(directory_path, sizeof(directory_path), "%s/%s",
                              home, folders[folder]);
        if (length < 0 || (size_t)length >= sizeof(directory_path)) {
            continue;
        }
        DIR *directory = opendir(directory_path);
        if (directory == NULL) {
            continue;
        }
        struct dirent *entry;
        while (count < capacity && (entry = readdir(directory)) != NULL) {
            if (entry->d_name[0] == '.' || !image_extension(entry->d_name)) {
                continue;
            }
            length = snprintf(paths[count], 4096, "%s/%s",
                             directory_path, entry->d_name);
            if (length > 0 && length < 4096) {
                ++count;
            }
        }
        closedir(directory);
    }
    return count;
}

int appearance_set_image_enabled(Appearance *appearance, int enabled)
{
    if (enabled && appearance->image_path[0] == '\0') {
        fprintf(stderr, "Select an image from ~/Pictures before enabling image wallpaper.\n");
        return 0;
    }
    if (enabled && !appearance_feh_available()) {
        fprintf(stderr, "feh is not installed; install it explicitly from Settings first.\n");
        return 0;
    }
    Appearance updated = *appearance;
    updated.image_enabled = enabled != 0;
    if (!appearance_save(&updated)) {
        return 0;
    }
    *appearance = updated;
    return 1;
}

static int executable_in_path(const char *name)
{
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
        char candidate[1024];
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

int appearance_feh_available(void)
{
    return executable_in_path("feh");
}

int appearance_install_feh(void)
{
    if (appearance_feh_available()) {
        return 1;
    }
    if (!executable_in_path("pacman") || !executable_in_path("pkexec")) {
        fprintf(stderr, "Graphical feh installation requires pacman and pkexec; install feh with your package manager.\n");
        return 0;
    }
    char *arguments[] = {
        "pkexec", "pacman", "-S", "--needed", "--noconfirm", "feh", NULL
    };
    pid_t child;
    int result = posix_spawnp(&child, arguments[0], NULL, NULL,
                             arguments, environ);
    if (result != 0) {
        fprintf(stderr, "Could not start graphical feh installation: %s\n",
                strerror(result));
        return 0;
    }
    return 1;
}

int appearance_apply_wallpaper(const Appearance *appearance)
{
    if (!appearance->image_enabled) {
        return 1;
    }
    if (!appearance_feh_available()) {
        fprintf(stderr, "feh was enabled but is no longer installed.\n");
        return 0;
    }
    char image_path[sizeof(appearance->image_path)];
    snprintf(image_path, sizeof(image_path), "%s", appearance->image_path);
    char *arguments[] = {
        "feh", "--no-fehbg", "--bg-fill", image_path, NULL
    };
    pid_t child;
    int result = posix_spawnp(&child, arguments[0], NULL, NULL,
                             arguments, environ);
    if (result != 0) {
        fprintf(stderr, "Could not start image wallpaper: %s\n",
                strerror(result));
        return 0;
    }
    return 1;
}

static int wallpaper_geometry_error;
static XErrorHandler previous_x_error_handler;

static int handle_wallpaper_geometry_error(Display *display,
                                           XErrorEvent *event)
{
    if (event->error_code == BadDrawable) {
        wallpaper_geometry_error = 1;
        return 0;
    }
    if (previous_x_error_handler != NULL) {
        return previous_x_error_handler(display, event);
    }
    return 0;
}

Pixmap appearance_wallpaper_pixmap(Display *display, unsigned int *width,
                                   unsigned int *height)
{
    if (display == NULL) {
        return None;
    }
    Window root = RootWindow(display, DefaultScreen(display));
    Atom root_pixmap = XInternAtom(display, "_XROOTPMAP_ID", False);
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *property = NULL;
    Pixmap pixmap = None;
    if (width != NULL) {
        *width = 0;
    }
    if (height != NULL) {
        *height = 0;
    }
    if (XGetWindowProperty(display, root, root_pixmap, 0, 1, False, XA_PIXMAP,
                           &actual_type, &actual_format, &item_count,
                           &bytes_after, &property) == Success &&
        property != NULL && actual_type == XA_PIXMAP &&
        actual_format == 32 && item_count == 1) {
        Pixmap candidate = (Pixmap)*(unsigned long *)property;
        Window geometry_root;
        int x;
        int y;
        unsigned int border_width;
        unsigned int depth;
        unsigned int pixmap_width;
        unsigned int pixmap_height;
        XSync(display, False);
        wallpaper_geometry_error = 0;
        previous_x_error_handler =
            XSetErrorHandler(handle_wallpaper_geometry_error);
        int geometry_valid = XGetGeometry(
            display, candidate, &geometry_root, &x, &y,
            &pixmap_width, &pixmap_height, &border_width, &depth);
        XSync(display, False);
        XSetErrorHandler(previous_x_error_handler);
        previous_x_error_handler = NULL;
        if (geometry_valid && !wallpaper_geometry_error) {
            pixmap = candidate;
            if (width != NULL) {
                *width = pixmap_width;
            }
            if (height != NULL) {
                *height = pixmap_height;
            }
        }
    }
    if (property != NULL) {
        XFree(property);
    }
    return pixmap;
}

void appearance_clear_wallpaper(Display *display)
{
    if (display == NULL) {
        return;
    }
    Window root = RootWindow(display, DefaultScreen(display));
    Atom root_pixmap = XInternAtom(display, "_XROOTPMAP_ID", False);
    Atom esetroot_pixmap = XInternAtom(display, "ESETROOT_PMAP_ID", False);
    XDeleteProperty(display, root, root_pixmap);
    XDeleteProperty(display, root, esetroot_pixmap);
}
