#ifndef FIELD_DESKTOP_APPEARANCE_H
#define FIELD_DESKTOP_APPEARANCE_H

#include <stddef.h>

typedef struct {
    const char *name;
    const char *background;
    const char *foreground;
    const char *accent;
    const char *panel;
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

#endif
