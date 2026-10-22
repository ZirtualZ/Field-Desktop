#ifndef FIELD_DESKTOP_SHELL_DESKTOP_VIEW_H
#define FIELD_DESKTOP_SHELL_DESKTOP_VIEW_H

#include "gtk_shell_internal.h"

void shell_refresh_desktop_windows(ShellState *state);
void shell_update_wallpapers(ShellState *state);
void desktop_view_center_window(ShellState *state, GtkWidget *window,
                                int width, int height);

#endif
