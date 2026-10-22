#ifndef FIELD_DESKTOP_SHELL_SESSION_H
#define FIELD_DESKTOP_SHELL_SESSION_H

#include <sys/types.h>

int session_running_on_virtual_console(void);
void session_start_x_server(const char *session_argument);
pid_t session_start_window_manager(void);

#endif
