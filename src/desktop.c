#define _POSIX_C_SOURCE 200809L

#include "shell/gtk_shell.h"
#include "shell/session.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int own_session = argc > 1 && strcmp(argv[1], "--x-session") == 0;
    const char *display = getenv("DISPLAY");
    if (!own_session &&
        ((display == NULL || display[0] == '\0') ||
         session_running_on_virtual_console())) {
        session_start_x_server("--x-session");
    }

    if (signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
        perror("Could not configure child-process handling");
        return 1;
    }
    pid_t window_manager = own_session
        ? session_start_window_manager() : -1;
    if (own_session && window_manager < 0) {
        return 1;
    }

    int result = gtk_shell_run(argc, argv, window_manager);
    if (window_manager > 0) {
        int status;
        pid_t child_state;
        do {
            child_state = waitpid(window_manager, &status, WNOHANG);
        } while (child_state < 0 && errno == EINTR);
        if (child_state == 0 && kill(window_manager, SIGTERM) != 0 &&
            errno != ESRCH) {
            perror("Could not stop the window manager");
            result = 1;
        }
        while (waitpid(window_manager, NULL, 0) < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != ECHILD) {
                perror("Could not wait for the window manager");
                result = 1;
            }
            break;
        }
    }
    return result;
}
