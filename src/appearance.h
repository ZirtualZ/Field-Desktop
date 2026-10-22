#ifndef FIELD_DESKTOP_APPEARANCE_H
#define FIELD_DESKTOP_APPEARANCE_H

#include <X11/Xlib.h>

#include <stddef.h>

typedef struct {
    const char *name;
    const char *background;
    const char *foreground;
    const char *accent;
    const char *panel;
    const char *glow;
} AppearanceTheme;

typedef struct {
    int theme;
    int background;
    int image_enabled;
    char image_path[4096];
} Appearance;

void appearance_init(Appearance *appearance);
int appearance_save(const Appearance *appearance);
size_t appearance_theme_count(void);
const AppearanceTheme *appearance_theme_at(size_t index);
size_t appearance_background_count(void);
const char *appearance_background_name(int index);
const char *appearance_background_color(int index);
int appearance_select_theme(Appearance *appearance, int index);
int appearance_select_background(Appearance *appearance, int index);
int appearance_select_image(Appearance *appearance, const char *path);
size_t appearance_images_load(char paths[][4096], size_t capacity);
int appearance_set_image_enabled(Appearance *appearance, int enabled);
int appearance_feh_available(void);
int appearance_install_feh(void);
int appearance_apply_wallpaper(const Appearance *appearance);
Pixmap appearance_wallpaper_pixmap(Display *display, unsigned int *width,
                                   unsigned int *height);
void appearance_clear_wallpaper(Display *display);

#endif
