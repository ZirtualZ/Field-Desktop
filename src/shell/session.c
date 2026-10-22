#define _POSIX_C_SOURCE 200809L

#include "session.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void session_start_x_server(const char *session_argument)
{
    char executable[4096];
    ssize_t length = readlink("/proc/self/exe", executable,
                              sizeof(executable) - 1);
    if (length < 0 || (size_t)length >= sizeof(executable) - 1) {
        perror("Could not find desktop executable path");
        exit(1);
    }
    executable[length] = '\0';

    execlp("startx", "startx", executable, session_argument, (char *)NULL);
    perror("Could not start X. Install xorg-xinit and run this from a TTY.");
    exit(1);
}

int session_running_on_virtual_console(void)
{
    const char *terminal = ttyname(STDIN_FILENO);
    return terminal != NULL && strncmp(terminal, "/dev/tty", 8) == 0 &&
           isdigit((unsigned char)terminal[8]);
}

pid_t session_start_window_manager(void)
{
    int ready_pipe[2];
    if (pipe(ready_pipe) != 0) {
        perror("Could not create window-manager startup pipe");
        return -1;
    }

    pid_t desktop_pid = getpid();
    pid_t child = fork();
    if (child == 0) {
        close(ready_pipe[0]);
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 ||
            getppid() != desktop_pid) {
            _exit(127);
        }
        char executable[4096];
        ssize_t length = readlink("/proc/self/exe", executable,
                                  sizeof(executable) - 1);
        if (length < 0 || (size_t)length >= sizeof(executable) - 1) {
            perror("Could not locate the window manager");
            _exit(127);
        }
        executable[length] = '\0';
        char *slash = strrchr(executable, '/');
        if (slash == NULL) {
            _exit(127);
        }
        slash[1] = '\0';
        char manager_path[4096];
        int manager_length = snprintf(manager_path, sizeof(manager_path),
                                      "%ssimple-wm", executable);
        if (manager_length < 0 ||
            (size_t)manager_length >= sizeof(manager_path)) {
            _exit(127);
        }
        char descriptor[32];
        snprintf(descriptor, sizeof(descriptor), "%d", ready_pipe[1]);
        execl(manager_path, "simple-wm", "--ready-fd", descriptor,
              (char *)NULL);
        perror("Could not start the handmade window manager");
        _exit(127);
    }
    if (child < 0) {
        close(ready_pipe[0]);
        close(ready_pipe[1]);
        perror("Could not start the handmade window manager");
        return -1;
    }

    close(ready_pipe[1]);
    char ready = '0';
    ssize_t count;
    do {
        count = read(ready_pipe[0], &ready, 1);
    } while (count < 0 && errno == EINTR);
    close(ready_pipe[0]);
    if (count != 1 || ready != '1') {
        fprintf(stderr, "Could not start the handmade WM (is another WM already running?)\n");
        kill(child, SIGTERM);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
        }
        return -1;
    }
    return child;
}
