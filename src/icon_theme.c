#define _POSIX_C_SOURCE 200809L

#include "icon_theme.h"

#include <X11/Xutil.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
    MAX_ICON_THEMES = 128,
    MAX_ICON_ROOTS = 12,
    MAX_ICON_DIRECTORIES = 256,
    ICON_SIZE = 48,
    ICON_PATH_SIZE = 4096
};

typedef struct {
    char name[128];
    char root[ICON_PATH_SIZE];
} IconThemeEntry;

static const char *const icon_names[DESKTOP_ICON_COUNT][4] = {
    {"view-app-grid", "applications-all", "start-here", NULL},
    {"utilities-terminal", "terminal", "org.gnome.Terminal", NULL},
    {"system-file-manager", "folder", "system-file-manager-symbolic", NULL},
    {"web-browser", "internet-web-browser", "browser", NULL},
    {"preferences-system", "configure", "preferences-desktop", NULL},
    {"help-browser", "help-about", "help-contents", NULL},
    {"folder", "folder-symbolic", "inode-directory", NULL},
    {"text-x-generic", "application-octet-stream", "unknown", NULL}
};

static IconThemeEntry themes[MAX_ICON_THEMES];
static size_t theme_count;
static size_t selected_theme;
static char icon_roots[MAX_ICON_ROOTS][ICON_PATH_SIZE];
static size_t icon_root_count;
static Pixmap cache[DESKTOP_ICON_COUNT];
static Display *cache_display;
static unsigned long cache_background;

static int path_is_directory(const char *path)
{
    struct stat status;
    return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

static void add_icon_root(const char *path)
{
    if (path == NULL || path[0] == '\0' ||
        icon_root_count >= MAX_ICON_ROOTS || !path_is_directory(path)) {
        return;
    }
    for (size_t i = 0; i < icon_root_count; ++i) {
        if (strcmp(icon_roots[i], path) == 0) {
            return;
        }
    }
    snprintf(icon_roots[icon_root_count++], ICON_PATH_SIZE, "%s", path);
}

static void load_icon_roots(void)
{
    const char *home = getenv("HOME");
    char path[ICON_PATH_SIZE];
    if (home != NULL && home[0] != '\0') {
        if (snprintf(path, sizeof(path), "%s/.icons", home) <
            (int)sizeof(path)) {
            add_icon_root(path);
        }
    }

    const char *data_home = getenv("XDG_DATA_HOME");
    if (data_home != NULL && data_home[0] != '\0') {
        if (snprintf(path, sizeof(path), "%s/icons", data_home) <
            (int)sizeof(path)) {
            add_icon_root(path);
        }
    } else if (home != NULL && home[0] != '\0') {
        if (snprintf(path, sizeof(path), "%s/.local/share/icons", home) <
            (int)sizeof(path)) {
            add_icon_root(path);
        }
    }

    const char *data_dirs = getenv("XDG_DATA_DIRS");
    char *copy = strdup(data_dirs != NULL && data_dirs[0] != '\0'
                            ? data_dirs : "/usr/local/share:/usr/share");
    if (copy != NULL) {
        char *save = NULL;
        for (char *directory = strtok_r(copy, ":", &save);
             directory != NULL; directory = strtok_r(NULL, ":", &save)) {
            if (snprintf(path, sizeof(path), "%s/icons", directory) <
                (int)sizeof(path)) {
                add_icon_root(path);
            }
        }
        free(copy);
    }
}

static int theme_exists(const char *name)
{
    for (size_t i = 1; i < theme_count; ++i) {
        if (strcmp(themes[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void discover_themes(void)
{
    themes[0].name[0] = '\0';
    snprintf(themes[0].name, sizeof(themes[0].name), "Built-in");
    snprintf(themes[0].root, sizeof(themes[0].root), "builtin");
    theme_count = 1;
    for (size_t i = 0; i < icon_root_count; ++i) {
        DIR *root = opendir(icon_roots[i]);
        if (root == NULL) {
            continue;
        }
        struct dirent *entry;
        while ((entry = readdir(root)) != NULL &&
               theme_count < MAX_ICON_THEMES) {
            if (entry->d_name[0] == '.' || theme_exists(entry->d_name)) {
                continue;
            }
            char path[ICON_PATH_SIZE];
            int length = snprintf(path, sizeof(path), "%s/%s", icon_roots[i],
                                  entry->d_name);
            if (length < 0 || (size_t)length >= sizeof(path) ||
                !path_is_directory(path)) {
                continue;
            }
            char index_path[ICON_PATH_SIZE];
            length = snprintf(index_path, sizeof(index_path),
                              "%s/index.theme", path);
            if (length < 0 || (size_t)length >= sizeof(index_path) ||
                access(index_path, R_OK) != 0) {
                continue;
            }
            size_t name_length = strlen(entry->d_name);
            size_t root_length = strlen(icon_roots[i]);
            if (name_length >= sizeof(themes[theme_count].name) ||
                root_length >= sizeof(themes[theme_count].root)) {
                continue;
            }
            memcpy(themes[theme_count].name, entry->d_name, name_length + 1);
            memcpy(themes[theme_count].root, icon_roots[i], root_length + 1);
            ++theme_count;
        }
        closedir(root);
    }
}

static void save_selected_theme(void)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        fprintf(stderr, "Cannot save icon theme without a home directory.\n");
        return;
    }
    char directory[ICON_PATH_SIZE];
    char path[ICON_PATH_SIZE];
    int length = snprintf(directory, sizeof(directory),
                          "%s/.config/simple-desktop", home);
    if (length < 0 || (size_t)length >= sizeof(directory) ||
        (mkdir(directory, 0700) != 0 && errno != EEXIST)) {
        perror("Could not create icon theme settings directory");
        return;
    }
    length = snprintf(path, sizeof(path), "%s/icon-theme", directory);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        fprintf(stderr, "Icon theme settings path is too long.\n");
        return;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        perror("Could not save icon theme");
        return;
    }
    int failed = fprintf(file, "%s\n", themes[selected_theme].name) < 0;
    if (fclose(file) != 0) {
        failed = 1;
    }
    if (failed) {
        perror("Could not save icon theme");
    }
}

void icon_theme_initialize(Display *display)
{
    cache_display = display;
    load_icon_roots();
    discover_themes();
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return;
    }
    char path[ICON_PATH_SIZE];
    int length = snprintf(path, sizeof(path),
                          "%s/.config/simple-desktop/icon-theme", home);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        return;
    }
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    char saved[128];
    if (fgets(saved, sizeof(saved), file) != NULL) {
        saved[strcspn(saved, "\r\n")] = '\0';
        for (size_t i = 0; i < theme_count; ++i) {
            if (strcmp(themes[i].name, saved) == 0) {
                selected_theme = i;
                break;
            }
        }
    }
    fclose(file);
}

size_t icon_theme_count(void)
{
    return theme_count;
}

const char *icon_theme_name(size_t index)
{
    return index < theme_count ? themes[index].name : NULL;
}

size_t icon_theme_selected(void)
{
    return selected_theme;
}

int icon_theme_select(Display *display, size_t index)
{
    if (index >= theme_count) {
        return 0;
    }
    if (selected_theme == index) {
        return 1;
    }
    selected_theme = index;
    icon_theme_clear(display);
    save_selected_theme();
    return 1;
}

void icon_theme_clear(Display *display)
{
    for (int i = 0; i < DESKTOP_ICON_COUNT; ++i) {
        if (cache[i] != None) {
            XFreePixmap(display, cache[i]);
            cache[i] = None;
        }
    }
    cache_display = display;
    cache_background = 0;
}

static int read_all(int descriptor, unsigned char *buffer, size_t size)
{
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = read(descriptor, buffer + offset, size - offset);
        if (count > 0) {
            offset += (size_t)count;
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            return 0;
        }
    }
    return 1;
}

static int convert_rgba(const char *path,
                        unsigned char pixels[ICON_SIZE * ICON_SIZE * 4])
{
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return 0;
    }
    pid_t child = fork();
    if (child == 0) {
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(pipe_fds[1]);
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        execlp("convert", "convert", path, "-background", "none",
               "-alpha", "on", "-resize", "48x48", "-gravity", "center",
               "-extent", "48x48", "-depth", "8", "rgba:-", (char *)NULL);
        execlp("magick", "magick", "convert", path, "-background", "none",
               "-alpha", "on", "-resize", "48x48", "-gravity", "center",
               "-extent", "48x48", "-depth", "8", "rgba:-", (char *)NULL);
        _exit(127);
    }
    close(pipe_fds[1]);
    if (child < 0) {
        close(pipe_fds[0]);
        return 0;
    }
    int success = read_all(pipe_fds[0], pixels, ICON_SIZE * ICON_SIZE * 4);
    close(pipe_fds[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return success && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static size_t theme_directories(const char *theme_path,
                                char directories[][128], size_t capacity)
{
    char index_path[ICON_PATH_SIZE];
    int length = snprintf(index_path, sizeof(index_path),
                          "%s/index.theme", theme_path);
    if (length < 0 || (size_t)length >= sizeof(index_path)) {
        return 0;
    }
    FILE *file = fopen(index_path, "r");
    if (file == NULL) {
        return 0;
    }
    char line[4096];
    int in_theme_section = 0;
    size_t count = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (line[0] == '[') {
            in_theme_section = strcmp(line, "[Icon Theme]\n") == 0 ||
                               strcmp(line, "[Icon Theme]\r\n") == 0;
            continue;
        }
        if (!in_theme_section || strncmp(line, "Directories=", 12) != 0) {
            continue;
        }
        char *save = NULL;
        for (char *part = strtok_r(line + 12, ",\r\n", &save);
             part != NULL && count < capacity;
             part = strtok_r(NULL, ",\r\n", &save)) {
            if (part[0] != '\0') {
                snprintf(directories[count++], 128, "%s", part);
            }
        }
        break;
    }
    fclose(file);
    return count;
}

static int find_icon_file(const char *theme_path, const char *name,
                          char output[ICON_PATH_SIZE])
{
    static const char *const extensions[] = {".png", ".svg", ".xpm"};
    char directories[MAX_ICON_DIRECTORIES][128];
    size_t count = theme_directories(theme_path, directories,
                                     MAX_ICON_DIRECTORIES);
    if (count == 0) {
        snprintf(directories[0], sizeof(directories[0]), "48x48/apps");
        snprintf(directories[1], sizeof(directories[1]), "48x48/places");
        snprintf(directories[2], sizeof(directories[2]), "scalable/apps");
        snprintf(directories[3], sizeof(directories[3]), "scalable/places");
        count = 4;
    }
    for (size_t directory = 0; directory < count; ++directory) {
        for (size_t extension = 0;
             extension < sizeof(extensions) / sizeof(extensions[0]);
             ++extension) {
            int length = snprintf(output, ICON_PATH_SIZE, "%s/%s/%s%s",
                                  theme_path, directories[directory], name,
                                  extensions[extension]);
            if (length > 0 && (size_t)length < ICON_PATH_SIZE &&
                access(output, R_OK) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

static int resolve_icon_path(const char *name, char output[ICON_PATH_SIZE])
{
    size_t order[3] = {selected_theme, 0, 0};
    size_t order_count = selected_theme == 0 ? 1 : 2;
    if (selected_theme != 0) {
        for (size_t i = 1; i < theme_count; ++i) {
            if (strcmp(themes[i].name, "hicolor") == 0) {
                order[1] = i;
                break;
            }
        }
    }
    for (size_t i = 0; i < order_count; ++i) {
        if (order[i] == 0) {
            continue;
        }
        char theme_path[ICON_PATH_SIZE];
        int length = snprintf(theme_path, sizeof(theme_path), "%s/%s",
                              themes[order[i]].root, themes[order[i]].name);
        if (length < 0 || (size_t)length >= sizeof(theme_path)) {
            continue;
        }
        if (find_icon_file(theme_path, name, output)) {
            return 1;
        }
    }
    return 0;
}

static unsigned long channel_pixel(unsigned char channel, unsigned long mask)
{
    if (mask == 0) {
        return 0;
    }
    unsigned int shift = 0;
    while (((mask >> shift) & 1UL) == 0UL) {
        ++shift;
    }
    unsigned long maximum = mask >> shift;
    return ((((unsigned long)channel * maximum + 127UL) / 255UL) << shift) &
           mask;
}

static Pixmap load_pixmap(Display *display, const char *name,
                          unsigned long background)
{
    char path[ICON_PATH_SIZE];
    if (!resolve_icon_path(name, path)) {
        return None;
    }
    unsigned char rgba[ICON_SIZE * ICON_SIZE * 4];
    if (!convert_rgba(path, rgba)) {
        return None;
    }

    Visual *visual = DefaultVisual(display, DefaultScreen(display));
    if (visual->class != TrueColor && visual->class != DirectColor) {
        return None;
    }
    XColor background_color;
    background_color.pixel = background;
    XQueryColor(display, DefaultColormap(display, DefaultScreen(display)),
                &background_color);
    XImage *image = XCreateImage(display, visual,
                                 (unsigned int)DefaultDepth(
                                     display, DefaultScreen(display)),
                                 ZPixmap, 0, NULL, ICON_SIZE, ICON_SIZE, 32, 0);
    if (image == NULL) {
        return None;
    }
    image->data = calloc((size_t)image->bytes_per_line,
                         (size_t)image->height);
    if (image->data == NULL) {
        image->f.destroy_image(image);
        return None;
    }
    for (int y = 0; y < ICON_SIZE; ++y) {
        for (int x = 0; x < ICON_SIZE; ++x) {
            size_t offset = ((size_t)y * ICON_SIZE + (size_t)x) * 4;
            unsigned int alpha = rgba[offset + 3];
            unsigned char red = (unsigned char)(
                (rgba[offset] * alpha +
                 (background_color.red >> 8) * (255U - alpha) + 127U) / 255U);
            unsigned char green = (unsigned char)(
                (rgba[offset + 1] * alpha +
                 (background_color.green >> 8) * (255U - alpha) + 127U) / 255U);
            unsigned char blue = (unsigned char)(
                (rgba[offset + 2] * alpha +
                 (background_color.blue >> 8) * (255U - alpha) + 127U) / 255U);
            unsigned long pixel = channel_pixel(red, visual->red_mask) |
                channel_pixel(green, visual->green_mask) |
                channel_pixel(blue, visual->blue_mask);
            XPutPixel(image, x, y, pixel);
        }
    }
    Pixmap pixmap = XCreatePixmap(display, DefaultRootWindow(display),
                                  ICON_SIZE, ICON_SIZE,
                                  (unsigned int)DefaultDepth(
                                      display, DefaultScreen(display)));
    GC pixmap_gc = XCreateGC(display, pixmap, 0, NULL);
    XPutImage(display, pixmap, pixmap_gc, image,
              0, 0, 0, 0, ICON_SIZE, ICON_SIZE);
    XFreeGC(display, pixmap_gc);
    XDestroyImage(image);
    return pixmap;
}

int icon_theme_draw(Display *display, Drawable drawable, GC gc, int icon,
                    int x, int y, unsigned long background)
{
    if (icon < 0 || icon >= DESKTOP_ICON_COUNT || selected_theme == 0) {
        return 0;
    }
    if (cache_display != display || cache_background != background) {
        icon_theme_clear(display);
        cache_display = display;
        cache_background = background;
    }
    if (cache[icon] == None) {
        for (int i = 0; i < 4 && icon_names[icon][i] != NULL; ++i) {
            cache[icon] = load_pixmap(display, icon_names[icon][i], background);
            if (cache[icon] != None) {
                break;
            }
        }
    }
    if (cache[icon] == None) {
        return 0;
    }
    XCopyArea(display, cache[icon], drawable, gc, 0, 0,
              ICON_SIZE, ICON_SIZE, x, y);
    return 1;
}
