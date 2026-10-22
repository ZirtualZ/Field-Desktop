#ifndef FIELD_DESKTOP_SHELL_ICON_THEMES_H
#define FIELD_DESKTOP_SHELL_ICON_THEMES_H

#include "gtk_shell_internal.h"

void icon_themes_initialize(ShellState *state);
void icon_themes_destroy(ShellState *state);
int icon_themes_select(ShellState *state, const char *theme_id);

#endif
