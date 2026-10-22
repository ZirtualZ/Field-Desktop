#define _POSIX_C_SOURCE 200809L

#include "applications.h"
#include "animations.h"
#include "appearance.h"
#include "desktop_items.h"
#include "startup_apps.h"

#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum {
    ICON_WIDTH = 84,
    ICON_HEIGHT = 64,
    LABEL_HEIGHT = 24,
    MAX_TERMINALS = 4,
    MAX_APPLICATIONS = 128,
    LAUNCHER_WIDTH = 520,
    LAUNCHER_HEIGHT = 430,
    LAUNCHER_ROWS = 10,
    MAX_RESOLUTIONS = 128,
    RESOLUTION_ROWS = 9,
    SETTINGS_WIDTH = 650,
    SETTINGS_HEIGHT = 470,
    HELP_WIDTH = 640,
    HELP_HEIGHT = 470,
    CONFIRM_WIDTH = 500,
    CONFIRM_HEIGHT = 130,
    CONFIRM_SECONDS = 15,
    MAX_DESKTOP_ITEMS = 128,
    DESKTOP_ITEM_COLUMNS = 9,
    DESKTOP_ITEM_ROWS = 6,
    SETTINGS_PAGE_GENERAL = 0,
    SETTINGS_PAGE_APPEARANCE = 1,
    SETTINGS_PAGE_OPTIONS = 2,
    APPEARANCE_IMAGE_ROWS = 6,
    MAX_TRAY_ICONS = 32,
    TRAY_ICON_SIZE = 24,
    TRAY_ICON_GAP = 4,
    XEMBED_MAPPED = 1,
    XEMBED_EMBEDDED_NOTIFY = 0,
    XEMBED_WINDOW_ACTIVATE = 1,
    XEMBED_WINDOW_DEACTIVATE = 2,
    XEMBED_VERSION = 0,
    SYSTEM_TRAY_REQUEST_DOCK = 0
};

typedef struct {
    Window window;
    int mapped;
} TrayIcon;

typedef struct {
    Window owner;
    Window window;
    Atom selection;
    Atom opcode;
    Atom xembed;
    Atom xembed_info;
    TrayIcon icons[MAX_TRAY_ICONS];
    int count;
    Window status_window;
    int status_width;
} SystemTray;

typedef struct {
    unsigned long long ticks;
    unsigned long long resident_kb;
} ProcessUsage;

typedef struct {
    unsigned long long total_kb;
    unsigned long long available_kb;
    unsigned long long system_ticks;
    ProcessUsage desktop;
    ProcessUsage manager;
    double desktop_cpu;
    double manager_cpu;
    int memory_valid;
    int desktop_valid;
    int manager_valid;
    int initialized;
    int manager_initialized;
} ResourceMonitor;

typedef struct {
    int width;
    int height;
    int mode_id;
} Resolution;

typedef struct {
    const char *name;
    const char *label;
    const char *option;
} TerminalOption;

static const TerminalOption terminal_options[] = {
    {"konsole", "Konsole", "--separate"},
    {"xfce4-terminal", "XFCE Terminal", "--disable-server"},
    {"xterm", "XTerm", NULL},
    {"gnome-terminal", "GNOME Terminal", "--wait"}
};

static int available_terminals[MAX_TERMINALS];
static int terminal_count;
static Application applications[MAX_APPLICATIONS];
static size_t application_count;
static SystemTray system_tray;
static ResourceMonitor resource_monitor;
static int performance_stats_enabled;
static Resolution supported_resolutions[MAX_RESOLUTIONS];
static int supported_resolution_count;
static int selected_resolution;
static int active_resolution;
static int resolution_scroll;
static XRRScreenConfiguration *randr_configuration;
static int randr_event_base;
static int randr_error_base;
static int help_view;
static int resolution_confirmation;
static int confirmation_seconds;
static time_t confirmation_deadline;
static Resolution previous_resolution;
static Appearance appearance;
static DesktopAnimations animations;
static StartupApps startup_apps;
static DesktopItems desktop_items;
static int selected_desktop_item = -1;
static int settings_page;
static char wallpaper_candidates[64][4096];
static size_t wallpaper_candidate_count;
static size_t wallpaper_scroll;
static int wallpaper_applied;

static int desktop_item_visible_rows(int height)
{
    int rows = (height - 442) / 94 + 1;
    if (rows > DESKTOP_ITEM_ROWS) {
        rows = DESKTOP_ITEM_ROWS;
    }
    return rows < 1 ? 1 : rows;
}

static int find_executable(const char *name)
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

static void start_x_server(const char *session_argument)
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

static int running_on_virtual_console(void)
{
    const char *terminal = ttyname(STDIN_FILENO);
    return terminal != NULL && strncmp(terminal, "/dev/tty", 8) == 0 &&
           isdigit((unsigned char)terminal[8]);
}

static pid_t start_window_manager(void)
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
        if (snprintf(manager_path, sizeof(manager_path), "%ssimple-wm", executable) >=
            (int)sizeof(manager_path)) {
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

static void terminal_settings_path(char *path, size_t path_size)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        path[0] = '\0';
        return;
    }

    char config_directory[1024];
    snprintf(config_directory, sizeof(config_directory), "%s/.config", home);
    mkdir(config_directory, 0700);
    snprintf(config_directory, sizeof(config_directory),
             "%s/.config/simple-desktop", home);
    mkdir(config_directory, 0700);
    snprintf(path, path_size, "%s/terminal", config_directory);
}

static void performance_settings_path(char *path, size_t path_size)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        path[0] = '\0';
        return;
    }
    int length = snprintf(path, path_size,
                          "%s/.config/simple-desktop/performance", home);
    if (length < 0 || (size_t)length >= path_size) {
        path[0] = '\0';
    }
}

static int load_performance_stats(void)
{
    char path[1200];
    performance_settings_path(path, sizeof(path));
    FILE *file = path[0] != '\0' ? fopen(path, "r") : NULL;
    if (file == NULL) {
        return 0;
    }
    char saved[16];
    int enabled = fgets(saved, sizeof(saved), file) != NULL &&
                  (strcmp(saved, "1\n") == 0 || strcmp(saved, "1") == 0);
    fclose(file);
    return enabled;
}

static void save_performance_stats(int enabled)
{
    char directory[1200];
    terminal_settings_path(directory, sizeof(directory));
    char path[1200];
    performance_settings_path(path, sizeof(path));
    if (path[0] == '\0') {
        fprintf(stderr, "Cannot save performance setting without a home directory.\n");
        return;
    }
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        perror("Could not save performance setting");
        return;
    }
    int write_failed = fprintf(file, "%d\n", enabled) < 0;
    if (fclose(file) != 0) {
        write_failed = 1;
    }
    if (write_failed) {
        perror("Could not save performance setting");
    }
}

static int load_terminal_selection(void)
{
    int selected = 0;
    char path[1200];
    terminal_settings_path(path, sizeof(path));
    if (path[0] != '\0') {
        FILE *file = fopen(path, "r");
        if (file != NULL) {
            char saved[128];
            if (fgets(saved, sizeof(saved), file) != NULL) {
                saved[strcspn(saved, "\r\n")] = '\0';
                for (int i = 0; i < terminal_count; ++i) {
                    if (strcmp(saved, terminal_options[available_terminals[i]].name) == 0) {
                        selected = i;
                        break;
                    }
                }
            }
            fclose(file);
        }
    }

    return selected;
}

static void save_terminal_selection(int selection)
{
    char path[1200];
    terminal_settings_path(path, sizeof(path));
    if (path[0] == '\0') {
        return;
    }

    FILE *file = fopen(path, "w");
    if (file != NULL) {
        fprintf(file, "%s\n", terminal_options[available_terminals[selection]].name);
        fclose(file);
    }
}

static unsigned long color(Display *display, int screen, const char *name)
{
    XColor exact;
    XColor allocated;
    Colormap colormap = DefaultColormap(display, screen);

    if (!XAllocNamedColor(display, colormap, name, &allocated, &exact)) {
        return BlackPixel(display, screen);
    }

    return allocated.pixel;
}

static void reap_child_processes(pid_t *window_manager)
{
    int status;
    pid_t child;
    do {
        child = waitpid(-1, &status, WNOHANG);
        if (child > 0 && *window_manager == child) {
            *window_manager = -1;
            fprintf(stderr,
                    "Window manager exited; managed windows are unavailable.\n");
        }
    } while (child > 0 || (child < 0 && errno == EINTR));
    if (child < 0 && errno != ECHILD) {
        perror("Could not reap child process");
    }
}

static void set_appearance_palette(Display *display, int screen,
                                   unsigned long *background,
                                   unsigned long *foreground,
                                   unsigned long *accent,
                                   unsigned long *panel,
                                   unsigned long *glow)
{
    const AppearanceTheme *theme =
        appearance_theme_at((size_t)appearance.theme);
    if (theme == NULL) {
        return;
    }
    const char *background_name =
        appearance_background_color(appearance.background);
    *background = color(display, screen,
                        background_name != NULL ? background_name :
                                                  theme->background);
    *foreground = color(display, screen, theme->foreground);
    *accent = color(display, screen, theme->accent);
    *panel = color(display, screen, theme->panel);
    *glow = color(display, screen, theme->glow);
}

static void refresh_supported_resolutions(Display *display, Window root)
{
    if (randr_configuration != NULL) {
        XRRFreeScreenConfigInfo(randr_configuration);
        randr_configuration = NULL;
    }
    supported_resolution_count = 0;
    if (!XRRQueryExtension(display, &randr_event_base, &randr_error_base)) {
        fprintf(stderr, "RandR extension unavailable; resolution selection is disabled.\n");
        return;
    }

    randr_configuration = XRRGetScreenInfo(display, root);
    if (randr_configuration == NULL) {
        fprintf(stderr, "Could not read display resolutions.\n");
        return;
    }
    int size_count = 0;
    XRRScreenSize *sizes = XRRConfigSizes(randr_configuration, &size_count);
    for (int i = 0; sizes != NULL && i < size_count &&
         supported_resolution_count < MAX_RESOLUTIONS; ++i) {
        int duplicate = 0;
        for (int j = 0; j < supported_resolution_count; ++j) {
            if (supported_resolutions[j].width == sizes[i].width &&
                supported_resolutions[j].height == sizes[i].height) {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate) {
            supported_resolutions[supported_resolution_count++] =
                (Resolution){sizes[i].width, sizes[i].height, i};
        }
    }

    Rotation rotation;
    SizeID current = XRRConfigCurrentConfiguration(randr_configuration,
                                                    &rotation);
    (void)rotation;
    selected_resolution = current < supported_resolution_count
        ? (int)current : 0;
    active_resolution = selected_resolution;
    for (int i = 0; i < supported_resolution_count; ++i) {
        if (supported_resolutions[i].width == DisplayWidth(display,
                                                            DefaultScreen(display)) &&
            supported_resolutions[i].height == DisplayHeight(display,
                                                              DefaultScreen(display))) {
            selected_resolution = i;
            active_resolution = i;
            break;
        }
    }
    if (resolution_scroll > supported_resolution_count - RESOLUTION_ROWS) {
        resolution_scroll = supported_resolution_count > RESOLUTION_ROWS
            ? supported_resolution_count - RESOLUTION_ROWS : 0;
    }
}

static int set_resolution(Display *display, Window root, int index)
{
    if (randr_configuration == NULL || index < 0 ||
        index >= supported_resolution_count) {
        return 0;
    }
    Rotation rotation;
    XRRConfigCurrentConfiguration(randr_configuration, &rotation);
    Status status = XRRSetScreenConfig(
        display, randr_configuration, root,
        (SizeID)supported_resolutions[index].mode_id, rotation, CurrentTime);
    if (status != RRSetConfigSuccess) {
        fprintf(stderr, "Could not apply the selected display resolution.\n");
        refresh_supported_resolutions(display, root);
        return 0;
    }
    return 1;
}

static void start_resolution_confirmation(Display *display, Window root,
                                          int index)
{
    if (index < 0 || index >= supported_resolution_count ||
        index == active_resolution) {
        return;
    }
    previous_resolution = supported_resolutions[active_resolution];
    if (!set_resolution(display, root, index)) {
        return;
    }
    selected_resolution = index;
    active_resolution = index;
    resolution_confirmation = 1;
    confirmation_deadline = time(NULL) + CONFIRM_SECONDS;
    confirmation_seconds = CONFIRM_SECONDS;
}

static void finish_resolution_confirmation(Display *display, Window root,
                                           int keep)
{
    if (!resolution_confirmation) {
        return;
    }
    resolution_confirmation = 0;
    if (!keep) {
        int previous_index = -1;
        for (int i = 0; i < supported_resolution_count; ++i) {
            if (supported_resolutions[i].width == previous_resolution.width &&
                supported_resolutions[i].height == previous_resolution.height) {
                previous_index = i;
                break;
            }
        }
        if (previous_index >= 0) {
            set_resolution(display, root, previous_index);
            selected_resolution = previous_index;
            active_resolution = previous_index;
        } else {
            fprintf(stderr, "Previous display resolution is no longer available to restore.\n");
        }
    }
    refresh_supported_resolutions(display, root);
}

static int tray_icon_index(Window window)
{
    for (int i = 0; i < system_tray.count; ++i) {
        if (system_tray.icons[i].window == window) {
            return i;
        }
    }
    return -1;
}

static int tray_icon_requests_mapping(Display *display, Window icon)
{
    Atom actual_type;
    int actual_format;
    unsigned long item_count;
    unsigned long bytes_after;
    unsigned char *property = NULL;
    int mapped = 1;

    if (XGetWindowProperty(display, icon, system_tray.xembed_info, 0, 2,
                           False, system_tray.xembed_info, &actual_type,
                           &actual_format, &item_count, &bytes_after,
                           &property) == Success &&
        property != NULL && actual_format == 32 && item_count >= 2) {
        unsigned long *values = (unsigned long *)property;
        mapped = (values[1] & XEMBED_MAPPED) != 0;
    }
    if (property != NULL) {
        XFree(property);
    }
    return mapped;
}

static void tray_send_xembed(Display *display, Window icon, long message,
                             long detail, long data1, long data2)
{
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.window = icon;
    event.xclient.message_type = system_tray.xembed;
    event.xclient.format = 32;
    event.xclient.data.l[0] = CurrentTime;
    event.xclient.data.l[1] = message;
    event.xclient.data.l[2] = detail;
    event.xclient.data.l[3] = data1;
    event.xclient.data.l[4] = data2;
    XSendEvent(display, icon, False, NoEventMask, &event);
}

static int read_memory(unsigned long long *total_kb,
                       unsigned long long *available_kb)
{
    FILE *file = fopen("/proc/meminfo", "r");
    if (file == NULL) {
        return 0;
    }
    char line[256];
    int have_total = 0;
    int have_available = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "MemTotal: %llu kB", total_kb) == 1) {
            have_total = 1;
        } else if (sscanf(line, "MemAvailable: %llu kB", available_kb) == 1) {
            have_available = 1;
        }
    }
    fclose(file);
    return have_total && have_available && *total_kb >= *available_kb;
}

static int read_system_ticks(unsigned long long *ticks)
{
    FILE *file = fopen("/proc/stat", "r");
    if (file == NULL) {
        return 0;
    }
    unsigned long long values[8];
    int fields = fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                        &values[0], &values[1], &values[2], &values[3],
                        &values[4], &values[5], &values[6], &values[7]);
    fclose(file);
    if (fields != 8) {
        return 0;
    }
    *ticks = 0;
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        *ticks += values[i];
    }
    return 1;
}

static int read_process_usage(pid_t pid, ProcessUsage *usage)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }
    char line[4096];
    int read_line = fgets(line, sizeof(line), file) != NULL;
    fclose(file);
    if (!read_line) {
        return 0;
    }

    char *fields = strrchr(line, ')');
    if (fields == NULL) {
        return 0;
    }
    ++fields;
    unsigned long long user_ticks = 0;
    unsigned long long system_ticks = 0;
    for (int field = 0; field <= 12; ++field) {
        while (*fields == ' ') {
            ++fields;
        }
        if (*fields == '\0') {
            return 0;
        }
        char *end = fields;
        while (*end != '\0' && *end != ' ' && *end != '\n') {
            ++end;
        }
        if (field == 11) {
            user_ticks = strtoull(fields, NULL, 10);
        } else if (field == 12) {
            system_ticks = strtoull(fields, NULL, 10);
        }
        fields = end;
    }
    usage->ticks = user_ticks + system_ticks;

    snprintf(path, sizeof(path), "/proc/%ld/status", (long)pid);
    file = fopen(path, "r");
    if (file == NULL) {
        usage->resident_kb = 0;
        return 1;
    }
    usage->resident_kb = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "VmRSS: %llu kB", &usage->resident_kb) == 1) {
            break;
        }
    }
    fclose(file);
    return usage->resident_kb > 0;
}

static double process_cpu_percent(ProcessUsage current,
                                  ProcessUsage previous,
                                  unsigned long long system_delta)
{
    if (current.ticks < previous.ticks || system_delta == 0) {
        return 0.0;
    }
    long processors = sysconf(_SC_NPROCESSORS_ONLN);
    if (processors < 1) {
        processors = 1;
    }
    unsigned long long process_delta = current.ticks - previous.ticks;
    return (double)process_delta * 100.0 * (double)processors /
           (double)system_delta;
}

static void tray_status_redraw(Display *display, GC gc,
                               unsigned long foreground,
                               unsigned long panel)
{
    if (system_tray.status_window == None) {
        return;
    }
    int width = performance_stats_enabled ? 520 : 190;
    system_tray.status_width = width;
    int visible = 0;
    for (int i = 0; i < system_tray.count; ++i) {
        if (system_tray.icons[i].mapped) {
            ++visible;
        }
    }
    int tray_width = visible == 0 ? 0 :
        visible * TRAY_ICON_SIZE + (visible + 1) * TRAY_ICON_GAP;
    int screen = DefaultScreen(display);
    XMoveResizeWindow(display, system_tray.status_window,
                      DisplayWidth(display, screen) - tray_width - width - 24,
                      7, (unsigned int)width, TRAY_ICON_SIZE + TRAY_ICON_GAP * 2);
    XSetWindowBackground(display, system_tray.status_window, panel);
    XClearWindow(display, system_tray.status_window);

    char used[32];
    char total[32];
    unsigned long long used_kb =
        resource_monitor.total_kb >= resource_monitor.available_kb
            ? resource_monitor.total_kb - resource_monitor.available_kb : 0;
    if (resource_monitor.total_kb >= 1024ULL * 1024ULL) {
        snprintf(used, sizeof(used), "%.1fG", (double)used_kb / 1048576.0);
        snprintf(total, sizeof(total), "%.1fG",
                 (double)resource_monitor.total_kb / 1048576.0);
    } else {
        snprintf(used, sizeof(used), "%lluM", used_kb / 1024ULL);
        snprintf(total, sizeof(total), "%lluM",
                 resource_monitor.total_kb / 1024ULL);
    }
    char text[256];
    if (performance_stats_enabled && resource_monitor.desktop_valid &&
        resource_monitor.manager_valid) {
        snprintf(text, sizeof(text),
                 "RAM %s/%s  DESK %.1f%% %lluM  WM %.1f%% %lluM",
                 used, total, resource_monitor.desktop_cpu,
                 resource_monitor.desktop.resident_kb / 1024ULL,
                 resource_monitor.manager_cpu,
                 resource_monitor.manager.resident_kb / 1024ULL);
    } else if (performance_stats_enabled && resource_monitor.desktop_valid) {
        snprintf(text, sizeof(text), "RAM %s/%s  DESK %.1f%% %lluM  WM --",
                 used, total, resource_monitor.desktop_cpu,
                 resource_monitor.desktop.resident_kb / 1024ULL);
    } else if (performance_stats_enabled) {
        snprintf(text, sizeof(text), "RAM %s/%s  process stats unavailable",
                 used, total);
    } else if (resource_monitor.memory_valid) {
        snprintf(text, sizeof(text), "RAM %s / %s", used, total);
    } else {
        snprintf(text, sizeof(text), "RAM unavailable");
    }
    XSetForeground(display, gc, foreground);
    XDrawString(display, system_tray.status_window, gc, 8, 25, text,
                (int)strlen(text));
    XMapRaised(display, system_tray.status_window);
}

static void tray_update_monitor(Display *display, GC gc,
                                unsigned long foreground, unsigned long panel,
                                pid_t manager_pid)
{
    unsigned long long total_kb = 0;
    unsigned long long available_kb = 0;
    if (read_memory(&total_kb, &available_kb)) {
        resource_monitor.total_kb = total_kb;
        resource_monitor.available_kb = available_kb;
        resource_monitor.memory_valid = 1;
    } else {
        resource_monitor.memory_valid = 0;
    }
    if (performance_stats_enabled) {
        unsigned long long system_ticks = 0;
        ProcessUsage desktop = {0};
        ProcessUsage manager = {0};
        int have_system = read_system_ticks(&system_ticks);
        int have_desktop = read_process_usage(getpid(), &desktop);
        int have_manager = manager_pid > 0 &&
                           read_process_usage(manager_pid, &manager);
        if (resource_monitor.initialized && have_system && have_desktop &&
            system_ticks > resource_monitor.system_ticks) {
            resource_monitor.desktop_cpu = process_cpu_percent(
                desktop, resource_monitor.desktop,
                system_ticks - resource_monitor.system_ticks);
            if (have_manager && resource_monitor.manager_initialized) {
                resource_monitor.manager_cpu = process_cpu_percent(
                    manager, resource_monitor.manager,
                    system_ticks - resource_monitor.system_ticks);
            } else {
                resource_monitor.manager_cpu = 0.0;
            }
        }
        if (have_system) {
            resource_monitor.system_ticks = system_ticks;
        }
        if (have_desktop) {
            resource_monitor.desktop = desktop;
        }
        resource_monitor.desktop_valid = have_desktop;
        if (have_manager) {
            resource_monitor.manager = manager;
        }
        resource_monitor.manager_valid = have_manager;
        resource_monitor.manager_initialized = have_manager;
        resource_monitor.initialized = have_system && have_desktop;
    }
    tray_status_redraw(display, gc, foreground, panel);
}

static void tray_redraw(Display *display, GC gc, unsigned long foreground,
                        unsigned long panel)
{
    int visible = 0;
    for (int i = 0; i < system_tray.count; ++i) {
        if (system_tray.icons[i].mapped) {
            ++visible;
        }
    }
    int tray_width = visible * TRAY_ICON_SIZE + (visible + 1) * TRAY_ICON_GAP;
    if (system_tray.window != None) {
        if (visible == 0) {
            XUnmapWindow(display, system_tray.window);
        } else {
            int screen = DefaultScreen(display);
            XMoveResizeWindow(display, system_tray.window,
                              DisplayWidth(display, screen) - tray_width - 12, 7,
                              (unsigned int)tray_width,
                              TRAY_ICON_SIZE + TRAY_ICON_GAP * 2);
            XSetWindowBackground(display, system_tray.window, panel);
            XClearWindow(display, system_tray.window);
            int visible_index = 0;
            for (int i = 0; i < system_tray.count; ++i) {
                if (!system_tray.icons[i].mapped) {
                    continue;
                }
                XMoveResizeWindow(display, system_tray.icons[i].window,
                                  TRAY_ICON_GAP + visible_index *
                                      (TRAY_ICON_SIZE + TRAY_ICON_GAP),
                                  TRAY_ICON_GAP, TRAY_ICON_SIZE, TRAY_ICON_SIZE);
                ++visible_index;
            }
            XMapRaised(display, system_tray.window);
        }
    }
    tray_status_redraw(display, gc, foreground, panel);
}

static void tray_remove_icon(Display *display, Window icon,
                             GC gc, unsigned long foreground,
                             unsigned long panel)
{
    int index = tray_icon_index(icon);
    if (index < 0) {
        return;
    }
    for (int i = index; i + 1 < system_tray.count; ++i) {
        system_tray.icons[i] = system_tray.icons[i + 1];
    }
    --system_tray.count;
    tray_redraw(display, gc, foreground, panel);
}

static void tray_dock_icon(Display *display, Window icon,
                           GC gc, unsigned long foreground,
                           unsigned long panel)
{
    if (icon == None || tray_icon_index(icon) >= 0 ||
        system_tray.count >= MAX_TRAY_ICONS) {
        return;
    }
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(display, icon, &attributes)) {
        return;
    }

    int index = system_tray.count++;
    system_tray.icons[index].window = icon;
    system_tray.icons[index].mapped = tray_icon_requests_mapping(display, icon);
    XAddToSaveSet(display, icon);
    XSelectInput(display, icon, StructureNotifyMask | PropertyChangeMask);
    XSetWindowBorderWidth(display, icon, 0);
    XReparentWindow(display, icon, system_tray.window, 0, TRAY_ICON_GAP);
    tray_send_xembed(display, icon, XEMBED_EMBEDDED_NOTIFY, 0,
                     system_tray.window, XEMBED_VERSION);
    if (system_tray.icons[index].mapped) {
        XMapWindow(display, icon);
        tray_send_xembed(display, icon, XEMBED_WINDOW_ACTIVATE, 0, 0, 0);
    } else {
        XUnmapWindow(display, icon);
    }
    tray_redraw(display, gc, foreground, panel);
}

static void tray_initialize(Display *display, Window root,
                            GC gc, unsigned long foreground,
                            unsigned long panel)
{
    int screen = DefaultScreen(display);
    char selection_name[64];
    snprintf(selection_name, sizeof(selection_name),
             "_NET_SYSTEM_TRAY_S%d", screen);
    system_tray.selection = XInternAtom(display, selection_name, False);
    system_tray.opcode = XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", False);
    system_tray.xembed = XInternAtom(display, "_XEMBED", False);
    system_tray.xembed_info = XInternAtom(display, "_XEMBED_INFO", False);
    system_tray.status_width = 190;

    XSetWindowAttributes status_attributes;
    status_attributes.override_redirect = True;
    status_attributes.background_pixel = panel;
    status_attributes.event_mask = ExposureMask;
    system_tray.status_window = XCreateWindow(
        display, root, 0, 7, 190, TRAY_ICON_SIZE + TRAY_ICON_GAP * 2, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &status_attributes);
    tray_status_redraw(display, gc, foreground, panel);

    Window current_owner = XGetSelectionOwner(display, system_tray.selection);
    if (current_owner != None) {
        fprintf(stderr, "A system tray is already running on this display.\n");
        return;
    }

    system_tray.owner = XCreateSimpleWindow(display, root, -1, -1, 1, 1,
                                             0, panel, panel);
    XSelectInput(display, system_tray.owner, StructureNotifyMask);
    XSetSelectionOwner(display, system_tray.selection, system_tray.owner,
                       CurrentTime);
    XSync(display, False);
    if (XGetSelectionOwner(display, system_tray.selection) != system_tray.owner) {
        fprintf(stderr, "Could not claim the system-tray selection.\n");
        XDestroyWindow(display, system_tray.owner);
        system_tray.owner = None;
        return;
    }

    XSetWindowAttributes attributes;
    attributes.override_redirect = True;
    attributes.background_pixel = panel;
    attributes.event_mask = ExposureMask | SubstructureNotifyMask |
                            SubstructureRedirectMask;
    system_tray.window = XCreateWindow(
        display, root, 0, 0, 1, TRAY_ICON_SIZE + TRAY_ICON_GAP * 2, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);

    Atom orientation = XInternAtom(display, "_NET_SYSTEM_TRAY_ORIENTATION", False);
    Atom visual_atom = XInternAtom(display, "_NET_SYSTEM_TRAY_VISUAL", False);
    Atom cardinal = XInternAtom(display, "CARDINAL", False);
    Atom visualid = XInternAtom(display, "VISUALID", False);
    unsigned long horizontal = 0;
    unsigned long visual = XVisualIDFromVisual(DefaultVisual(display, screen));
    XChangeProperty(display, system_tray.owner, orientation, cardinal, 32,
                    PropModeReplace, (unsigned char *)&horizontal, 1);
    XChangeProperty(display, system_tray.owner, visual_atom, visualid, 32,
                    PropModeReplace, (unsigned char *)&visual, 1);

    Atom manager = XInternAtom(display, "MANAGER", False);
    XEvent announcement;
    memset(&announcement, 0, sizeof(announcement));
    announcement.xclient.type = ClientMessage;
    announcement.xclient.window = root;
    announcement.xclient.message_type = manager;
    announcement.xclient.format = 32;
    announcement.xclient.data.l[0] = CurrentTime;
    announcement.xclient.data.l[1] = system_tray.selection;
    announcement.xclient.data.l[2] = system_tray.owner;
    XSendEvent(display, root, False, StructureNotifyMask, &announcement);
}

static void update_desktop_geometry(Display *display, Window window,
                                    GC gc, unsigned long foreground,
                                    unsigned long panel, int *width,
                                    int *height)
{
    int screen = DefaultScreen(display);
    int new_width = DisplayWidth(display, screen);
    int new_height = DisplayHeight(display, screen);
    if (*width == new_width && *height == new_height) {
        return;
    }
    *width = new_width;
    *height = new_height;
    XMoveResizeWindow(display, window, 0, 0,
                      (unsigned int)new_width, (unsigned int)new_height);
    tray_redraw(display, gc, foreground, panel);
    XClearArea(display, window, 0, 0, 0, 0, True);
}

static void tray_shutdown(Display *display)
{
    if (system_tray.owner != None) {
        XSetSelectionOwner(display, system_tray.selection, None, CurrentTime);
    }
    if (system_tray.window != None) {
        for (int i = 0; i < system_tray.count; ++i) {
            tray_send_xembed(display, system_tray.icons[i].window,
                             XEMBED_WINDOW_DEACTIVATE, 0, 0, 0);
            XReparentWindow(display, system_tray.icons[i].window,
                            RootWindow(display, DefaultScreen(display)), 0, 0);
            XRemoveFromSaveSet(display, system_tray.icons[i].window);
        }
        XDestroyWindow(display, system_tray.window);
    }
    if (system_tray.status_window != None) {
        XDestroyWindow(display, system_tray.status_window);
    }
    if (system_tray.owner != None) {
        XDestroyWindow(display, system_tray.owner);
    }
}

static void draw_desktop(Display *display, Window window, GC gc,
                         int width, int height, unsigned long background,
                         unsigned long foreground, unsigned long accent,
                         unsigned long panel, unsigned long glow,
                         const DesktopItems *items, int selected_item)
{
    XSetForeground(display, gc, background);
    XFillRectangle(display, window, gc, 0, 0, (unsigned int)width,
                   (unsigned int)height);
    unsigned int wallpaper_width = 0;
    unsigned int wallpaper_height = 0;
    Pixmap wallpaper = appearance.image_enabled && wallpaper_applied
        ? appearance_wallpaper_pixmap(display, &wallpaper_width,
                                      &wallpaper_height) : None;
    if (wallpaper != None) {
        unsigned int copy_width = wallpaper_width < (unsigned int)width
            ? wallpaper_width : (unsigned int)width;
        unsigned int copy_height = wallpaper_height < (unsigned int)height
            ? wallpaper_height : (unsigned int)height;
        XCopyArea(display, wallpaper, window, gc, 0, 0,
                  copy_width, copy_height, 0, 0);
    } else {
        XSetForeground(display, gc, background);
        XSetForeground(display, gc, glow);
        XFillArc(display, window, gc, width - 430, height - 360,
                 520, 520, 0, 360 * 64);
        XSetForeground(display, gc, background);
        XFillArc(display, window, gc, width - 355, height - 305,
                 440, 440, 0, 360 * 64);
    }

    XSetForeground(display, gc, panel);
    XFillRectangle(display, window, gc, 0, 0, (unsigned int)width, 42);
    XSetForeground(display, gc, accent);
    XFillRectangle(display, window, gc, 18, 13, 16, 16);
    XSetForeground(display, gc, foreground);
    XDrawString(display, window, gc, 45, 26, "FIELD DESKTOP", 13);

    const int icon_x[] = {34, 150, 266, 34, 150, 266};
    const int icon_y[] = {84, 84, 84, 180, 180, 180};
    const char *labels[] = {
        "Apps", "Terminal", "Files", "Browser", "Settings", "Help"
    };
    for (int i = 0; i < 6; ++i) {
        XSetForeground(display, gc, panel);
        XFillRectangle(display, window, gc, icon_x[i] - 10, icon_y[i] - 8,
                       104, 86);
        XSetForeground(display, gc, foreground);
        if (i == 0) {
            XSetForeground(display, gc, accent);
            XFillRectangle(display, window, gc, icon_x[i] + 17, icon_y[i] + 12, 50, 34);
            XSetForeground(display, gc, panel);
            XFillRectangle(display, window, gc, icon_x[i] + 21, icon_y[i] + 16, 42, 26);
            XSetForeground(display, gc, foreground);
            XDrawString(display, window, gc, icon_x[i] + 26, icon_y[i] + 34, ">_", 2);
        } else {
            XSetForeground(display, gc, accent);
            XFillArc(display, window, gc, icon_x[i] + 26, icon_y[i] + 8,
                     34, 34, 0, 360 * 64);
            XSetForeground(display, gc, panel);
            XFillArc(display, window, gc, icon_x[i] + 31, icon_y[i] + 13,
                     24, 24, 0, 360 * 64);
            XSetForeground(display, gc, foreground);
            XDrawLine(display, window, gc, icon_x[i] + 43, icon_y[i] + 10,
                      icon_x[i] + 43, icon_y[i] + 40);
            XDrawLine(display, window, gc, icon_x[i] + 29, icon_y[i] + 25,
                      icon_x[i] + 57, icon_y[i] + 25);
        }
        XSetForeground(display, gc, foreground);
        XDrawString(display, window, gc, icon_x[i] + 9,
                    icon_y[i] + ICON_HEIGHT + 16, labels[i],
                    (int)strlen(labels[i]));
    }

    if (items != NULL && items->count > 0) {
        int rows = desktop_item_visible_rows(height);
        size_t visible = (size_t)rows * DESKTOP_ITEM_COLUMNS;
        if (visible > items->count) {
            visible = items->count;
        }
        for (size_t i = 0; i < visible; ++i) {
            const DesktopItem *item = desktop_items_get(items, i);
            int column = (int)(i % DESKTOP_ITEM_COLUMNS);
            int row = (int)(i / DESKTOP_ITEM_COLUMNS);
            int x = 24 + column * 116;
            int y = 270 + row * 94;
            if ((int)i == selected_item) {
                XSetForeground(display, gc, panel);
                XFillRectangle(display, window, gc, x, y, 104, 82);
            }
            XSetForeground(display, gc, accent);
            if (item->is_directory) {
                XFillRectangle(display, window, gc, x + 31, y + 12, 40, 27);
                XFillRectangle(display, window, gc, x + 26, y + 18, 50, 28);
            } else {
                XFillRectangle(display, window, gc, x + 34, y + 8, 34, 42);
                XSetForeground(display, gc, panel);
                XDrawLine(display, window, gc, x + 40, y + 20, x + 62, y + 20);
                XDrawLine(display, window, gc, x + 40, y + 27, x + 62, y + 27);
                XSetForeground(display, gc, accent);
            }
            XSetForeground(display, gc, foreground);
            int name_length = (int)strlen(item->name);
            XDrawString(display, window, gc, x + 4, y + 69, item->name,
                        name_length > 12 ? 12 : name_length);
        }
        if (visible < items->count) {
            char overflow[64];
            int length = snprintf(overflow, sizeof(overflow),
                                  "+%zu more in Desktop folder",
                                  items->count - visible);
            XSetForeground(display, gc, foreground);
            XDrawString(display, window, gc, 24, height - 48,
                        overflow, length);
        }
    }
}

static int matching_application_count(const char *query)
{
    int count = 0;
    for (size_t i = 0; i < application_count; ++i) {
        if (application_matches(&applications[i], query)) {
            ++count;
        }
    }
    return count;
}

static int matching_application_at(const char *query, int position)
{
    for (size_t i = 0; i < application_count; ++i) {
        if (application_matches(&applications[i], query) && position-- == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void draw_launcher(Display *display, Window window, GC gc,
                          unsigned long foreground, unsigned long accent,
                          unsigned long panel, unsigned long subdued,
                          const char *query, int selected)
{
    XSetForeground(display, gc, panel);
    XFillRectangle(display, window, gc, 0, 0,
                   LAUNCHER_WIDTH, LAUNCHER_HEIGHT);
    XSetForeground(display, gc, accent);
    XFillRectangle(display, window, gc, 0, 0, 5, LAUNCHER_HEIGHT);
    XSetForeground(display, gc, foreground);
    XDrawString(display, window, gc, 28, 34, "APPLICATIONS", 12);
    XSetForeground(display, gc, subdued);
    const char *help = "Type to search  |  Enter to launch  |  Esc to close";
    XDrawString(display, window, gc, 28, 59, help, (int)strlen(help));

    XSetForeground(display, gc, panel);
    XFillRectangle(display, window, gc, 24, 76, LAUNCHER_WIDTH - 48, 36);
    XSetForeground(display, gc, foreground);
    char search_line[sizeof("Search: ") + 128];
    snprintf(search_line, sizeof(search_line), "Search: %s", query);
    XDrawString(display, window, gc, 34, 99, search_line, (int)strlen(search_line));

    int result_count = matching_application_count(query);
    int first = selected >= LAUNCHER_ROWS ? selected - LAUNCHER_ROWS + 1 : 0;
    int shown = 0;
    for (int position = first; position < result_count &&
         shown < LAUNCHER_ROWS; ++position, ++shown) {
        int application_index = matching_application_at(query, position);
        if (application_index < 0) {
            break;
        }
        int y = 124 + shown * 27;
        if (position == selected) {
            XSetForeground(display, gc, accent);
            XFillRectangle(display, window, gc, 24, y - 17,
                           LAUNCHER_WIDTH - 48, 24);
        }
        XSetForeground(display, gc, position == selected ? panel : foreground);
        XDrawString(display, window, gc, 36, y,
                    applications[application_index].name,
                    (int)strlen(applications[application_index].name) > 58
                        ? 58 : (int)strlen(applications[application_index].name));
    }
    if (result_count == 0) {
        XSetForeground(display, gc, subdued);
        XDrawString(display, window, gc, 36, 145,
                    "No matching installed applications", 34);
    }
}

static Window open_launcher(Display *display, Window root, Window existing,
                            GC gc, unsigned long foreground,
                            unsigned long accent, unsigned long panel,
                            unsigned long subdued, const char *query,
                            int selected,
                            const DesktopAnimations *animation_settings)
{
    if (existing != None) {
        XRaiseWindow(display, existing);
        XSetInputFocus(display, existing, RevertToParent, CurrentTime);
        return existing;
    }

    int screen = DefaultScreen(display);
    int x = (DisplayWidth(display, screen) - LAUNCHER_WIDTH) / 2;
    int y = (DisplayHeight(display, screen) - LAUNCHER_HEIGHT) / 2;
    XSetWindowAttributes attributes;
    attributes.override_redirect = True;
    attributes.background_pixel = panel;
    attributes.event_mask = ExposureMask | ButtonPressMask | KeyPressMask;
    Window launcher = XCreateWindow(
        display, root, x, y, LAUNCHER_WIDTH, LAUNCHER_HEIGHT, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel | CWEventMask, &attributes);
    animations_map_window(display, launcher, x, y, LAUNCHER_WIDTH,
                          LAUNCHER_HEIGHT, animation_settings);
    XSetInputFocus(display, launcher, RevertToParent, CurrentTime);
    draw_launcher(display, launcher, gc, foreground, accent, panel,
                  subdued, query, selected);
    XFlush(display);
    return launcher;
}

static void draw_terminal_settings(Display *display, Window window, GC gc,
                                   int width, int height,
                                   unsigned long foreground,
                                   unsigned long accent, unsigned long panel,
                                   unsigned long subdued, int selected,
                                   int perf_stats, int selected_mode,
                                   int scroll, int pending_seconds)
{
    int left = (width - SETTINGS_WIDTH) / 2;
    int top = (height - SETTINGS_HEIGHT) / 2;
    XSetForeground(display, gc, panel);
    XFillRectangle(display, window, gc, left, top,
                   SETTINGS_WIDTH, SETTINGS_HEIGHT);
    XSetForeground(display, gc, accent);
    XFillRectangle(display, window, gc, left, top, 4, SETTINGS_HEIGHT);
    XSetForeground(display, gc, foreground);
    XDrawString(display, window, gc, left + 30, top + 38, "SETTINGS", 8);
    XSetForeground(display, gc, accent);
    XDrawString(display, window, gc, left + 30, top + 68, "[ General ]", 11);
    XSetForeground(display, gc, subdued);
    XDrawString(display, window, gc, left + 160, top + 68, "Appearance", 10);
    XDrawString(display, window, gc, left + 300, top + 68, "More", 4);
    XSetForeground(display, gc, subdued);
    XDrawString(display, window, gc, left + 30, top + 92,
                "Terminal", 8);
    XDrawString(display, window, gc, left + 325, top + 92,
                "Screen resolution", 17);
    if (terminal_count == 0) {
        XSetForeground(display, gc, subdued);
        XDrawString(display, window, gc, left + 30, top + 118,
                    "No supported terminal is installed.", 35);
    }
    for (int i = 0; i < terminal_count; ++i) {
        int row_y = top + 126 + i * 30;
        if (i == selected) {
            XSetForeground(display, gc, accent);
            XFillRectangle(display, window, gc, left + 24, row_y - 19,
                           245, 26);
        }
        XSetForeground(display, gc, i == selected ? panel : foreground);
        XDrawString(display, window, gc, left + 35, row_y,
                    terminal_options[available_terminals[i]].label,
                    (int)strlen(terminal_options[available_terminals[i]].label));
    }
    XSetForeground(display, gc, perf_stats ? accent : foreground);
    const char *stats = perf_stats ? "[x] Performance stats in tray"
                                   : "[ ] Performance stats in tray";
    XDrawString(display, window, gc, left + 35, top + 270,
                stats, (int)strlen(stats));

    XSetForeground(display, gc, subdued);
    XDrawString(display, window, gc, left + 325, top + 112,
                "Left/Right select, Enter test", 29);
    for (int row = 0; row < RESOLUTION_ROWS; ++row) {
        int index = scroll + row;
        if (index >= supported_resolution_count) {
            break;
        }
        int row_y = top + 142 + row * 27;
        char label[64];
        snprintf(label, sizeof(label), "%d x %d",
                 supported_resolutions[index].width,
                 supported_resolutions[index].height);
        if (index == selected_mode) {
            XSetForeground(display, gc, accent);
            XFillRectangle(display, window, gc, left + 310, row_y - 18,
                           290, 24);
        }
        XSetForeground(display, gc, index == selected_mode ? panel : foreground);
        XDrawString(display, window, gc, left + 326, row_y, label,
                    (int)strlen(label));
    }
    XSetForeground(display, gc, subdued);
    if (supported_resolution_count == 0) {
        XDrawString(display, window, gc, left + 325, top + 142,
                    "RandR modes unavailable", 23);
    }
    XDrawString(display, window, gc, left + 325, top + 142 + RESOLUTION_ROWS * 27,
                "PgUp/PgDn scroll", 16);

    if (pending_seconds > 0) {
        char countdown[64];
        snprintf(countdown, sizeof(countdown), "Confirm new screen size? %d s",
                 pending_seconds);
        XSetForeground(display, gc, accent);
        XDrawString(display, window, gc, left + 30, top + 335,
                    countdown, (int)strlen(countdown));
        XSetForeground(display, gc, foreground);
        XDrawString(display, window, gc, left + 30, top + 358,
                    "Enter / click Keep    Esc / click Revert", 42);
    } else {
        XSetForeground(display, gc, subdued);
        XDrawString(display, window, gc, left + 30, top + 358,
                    "Click a mode to test it; confirm within 15 seconds.", 52);
    }
    XSetForeground(display, gc, subdued);
    XDrawString(display, window, gc, left + 30, top + 430,
                "H: help    Esc: close settings", 30);
}

static void draw_settings_page_tabs(Display *display, Window window, GC gc,
                                        int width, int height,
                                        unsigned long foreground,
                                        unsigned long accent,
                                        unsigned long subdued, int page)
    {
        int left = (width - SETTINGS_WIDTH) / 2;
        int top = (height - SETTINGS_HEIGHT) / 2;
        const int positions[] = {left + 30, left + 160, left + 300};
        const char *labels[] = {"General", "Appearance", "More"};
        for (int i = 0; i < 3; ++i) {
            XSetForeground(display, gc, page == i ? accent : subdued);
            char label[32];
            snprintf(label, sizeof(label), page == i ? "[ %s ]" : "%s",
                     labels[i]);
            XDrawString(display, window, gc, positions[i], top + 68,
                        label, (int)strlen(label));
        }
        XSetForeground(display, gc, foreground);
    }

static void draw_appearance_settings(Display *display, Window window, GC gc,
                                         int width, int height,
                                         unsigned long background,
                                         unsigned long foreground,
                                         unsigned long accent, unsigned long panel,
                                         unsigned long subdued)
    {
        int left = (width - SETTINGS_WIDTH) / 2;
        int top = (height - SETTINGS_HEIGHT) / 2;
        XSetForeground(display, gc, panel);
        XFillRectangle(display, window, gc, left, top,
                       SETTINGS_WIDTH, SETTINGS_HEIGHT);
        XSetForeground(display, gc, accent);
        XFillRectangle(display, window, gc, left, top, 4, SETTINGS_HEIGHT);
        XSetForeground(display, gc, foreground);
        XDrawString(display, window, gc, left + 30, top + 38, "APPEARANCE", 10);
        draw_settings_page_tabs(display, window, gc, width, height, foreground,
                                accent, subdued, SETTINGS_PAGE_APPEARANCE);

        const AppearanceTheme *theme =
            appearance_theme_at((size_t)appearance.theme);
        char theme_text[128];
        snprintf(theme_text, sizeof(theme_text), "Theme: %s  (click to cycle)",
                 theme != NULL ? theme->name : "Default");
        XSetForeground(display, gc, foreground);
        XDrawString(display, window, gc, left + 30, top + 108,
                    theme_text, (int)strlen(theme_text));
        XSetForeground(display, gc, background);
        XFillRectangle(display, window, gc, left + 410, top + 94, 34, 20);
        XSetForeground(display, gc, panel);
        XFillRectangle(display, window, gc, left + 450, top + 94, 34, 20);
        XSetForeground(display, gc, accent);
        XFillRectangle(display, window, gc, left + 490, top + 94, 34, 20);
        XSetForeground(display, gc, foreground);
        const char *background_name =
            appearance_background_name(appearance.background);
        char background_text[128];
        snprintf(background_text, sizeof(background_text),
                 "Solid background: %s  (click to cycle)",
                 background_name != NULL ? background_name : "Theme default");
        XDrawString(display, window, gc, left + 30, top + 140,
                    background_text, (int)strlen(background_text));

        XSetForeground(display, gc,
                       appearance.image_enabled ? accent : foreground);
        const char *image_state = appearance.image_enabled
            ? "[x] Image wallpaper enabled"
            : "[ ] Image wallpaper disabled";
        XDrawString(display, window, gc, left + 30, top + 180,
                    image_state, (int)strlen(image_state));
        XSetForeground(display, gc, subdued);
        XDrawString(display, window, gc, left + 30, top + 202,
                    "feh stays unused unless you enable an image wallpaper.", 54);

        if (!appearance_feh_available()) {
            XSetForeground(display, gc, accent);
            XDrawString(display, window, gc, left + 30, top + 236,
                        "[ Install feh (graphical authorization) ]", 43);
        } else if (appearance.image_enabled && !wallpaper_applied) {
            XSetForeground(display, gc, accent);
            XDrawString(display, window, gc, left + 30, top + 236,
                        "[ Apply selected wallpaper ]", 29);
        } else {
            XSetForeground(display, gc, subdued);
            XDrawString(display, window, gc, left + 30, top + 236,
                        "feh is installed; image application is still opt-in.", 52);
        }
        XSetForeground(display, gc, foreground);
        XDrawString(display, window, gc, left + 30, top + 272,
                    "Images from ~/Pictures and ~/Desktop:", 38);

        size_t visible_images = wallpaper_candidate_count - wallpaper_scroll;
        if (visible_images > APPEARANCE_IMAGE_ROWS) {
            visible_images = APPEARANCE_IMAGE_ROWS;
        }
        for (size_t i = 0; i < visible_images; ++i) {
            size_t image_index = wallpaper_scroll + i;
            const char *name = strrchr(wallpaper_candidates[image_index], '/');
            name = name != NULL ? name + 1 : wallpaper_candidates[image_index];
            int row_y = top + 300 + (int)i * 22;
            int active = strcmp(wallpaper_candidates[image_index],
                                appearance.image_path) == 0;
            if (active) {
                XSetForeground(display, gc, accent);
                XFillRectangle(display, window, gc, left + 24, row_y - 17,
                               SETTINGS_WIDTH - 48, 21);
            }
            XSetForeground(display, gc, active ? panel : foreground);
            XDrawString(display, window, gc, left + 35, row_y, name,
                        (int)strlen(name) > 70 ? 70 : (int)strlen(name));
        }
        if (wallpaper_candidate_count == 0) {
            XSetForeground(display, gc, subdued);
            XDrawString(display, window, gc, left + 30, top + 310,
                        "No image files found in those folders.", 39);
        }
        XSetForeground(display, gc, subdued);
        XDrawString(display, window, gc, left + 30, top + 450,
                    "PgUp/PgDn: more images; built-in backgrounds stay lightweight.",
                    64);
    }

static void draw_optional_settings(Display *display, Window window, GC gc,
                                       int width, int height,
                                       unsigned long foreground,
                                       unsigned long accent, unsigned long panel,
                                       unsigned long subdued)
    {
        int left = (width - SETTINGS_WIDTH) / 2;
        int top = (height - SETTINGS_HEIGHT) / 2;
        XSetForeground(display, gc, panel);
        XFillRectangle(display, window, gc, left, top,
                       SETTINGS_WIDTH, SETTINGS_HEIGHT);
        XSetForeground(display, gc, accent);
        XFillRectangle(display, window, gc, left, top, 4, SETTINGS_HEIGHT);
        XSetForeground(display, gc, foreground);
        XDrawString(display, window, gc, left + 30, top + 38, "OPTIONAL FEATURES", 17);
        draw_settings_page_tabs(display, window, gc, width, height, foreground,
                                accent, subdued, SETTINGS_PAGE_OPTIONS);

        XSetForeground(display, gc,
                       startup_apps.enabled ? accent : foreground);
        const char *startup = startup_apps.enabled
            ? "[x] Launch user startup apps at login"
            : "[ ] Launch user startup apps at login";
        XDrawString(display, window, gc, left + 30, top + 125,
                    startup, (int)strlen(startup));
        XSetForeground(display, gc, subdued);
        XDrawString(display, window, gc, left + 50, top + 151,
                    "Off by default; uses ~/.config/autostart next session.", 54);
        char startup_count[96];
        snprintf(startup_count, sizeof(startup_count),
                 "%zu eligible user startup entries found.", startup_apps.count);
        XDrawString(display, window, gc, left + 50, top + 176,
                    startup_count, (int)strlen(startup_count));

        XSetForeground(display, gc, animations.enabled ? accent : foreground);
        const char *animation = animations.enabled
            ? "[x] Enable lightweight menu animations"
            : "[ ] Enable lightweight menu animations";
        XDrawString(display, window, gc, left + 30, top + 235,
                    animation, (int)strlen(animation));
        XSetForeground(display, gc, subdued);
        XDrawString(display, window, gc, left + 50, top + 261,
                    "Off by default; no animation work when disabled.", 49);
        XSetForeground(display, gc, subdued);
        XDrawString(display, window, gc, left + 30, top + 450,
                    "Optional features stay idle until you enable them.", 51);
}

static void draw_help(Display *display, Window window, GC gc, int width,
                      int height, unsigned long foreground,
                      unsigned long accent, unsigned long panel,
                      unsigned long subdued)
{
    int left = (width - HELP_WIDTH) / 2;
    int top = (height - HELP_HEIGHT) / 2;
    XSetForeground(display, gc, panel);
    XFillRectangle(display, window, gc, left, top, HELP_WIDTH, HELP_HEIGHT);
    XSetForeground(display, gc, accent);
    XFillRectangle(display, window, gc, left, top, 4, HELP_HEIGHT);
    XSetForeground(display, gc, foreground);
    XDrawString(display, window, gc, left + 30, top + 38,
                "FIELD DESKTOP HELP", 18);

    const char *lines[] = {
        "APPLICATIONS",
        "Click Apps or press Alt+Space to search installed applications.",
        "Type to filter; Up/Down selects; Enter launches; Esc closes.",
        "",
        "WINDOWS",
        "Alt+Tab / Alt+Shift+Tab switches between open windows.",
        "Click a taskbar button to focus a window; +N more opens switcher.",
        "Drag title bars to move. Drag right/bottom edges to resize.",
        "Alt+left-drag moves; Alt+right-drag resizes; Alt+F4 closes.",
        "",
        "DESKTOP AND SETTINGS",
        "Double-click Desktop items to open them with their default app.",
        "Settings tabs cover terminal, display, appearance, and optional features.",
        "Resolution changes are temporary until confirmed; timeout reverts.",
        "The tray shows system RAM; optional stats show desktop and WM usage.",
        "",
        "Press Escape or click outside this panel to return."
    };
    int y = top + 75;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
        if (lines[i][0] == '\0') {
            y += 8;
            continue;
        }
        if (i == 0 || i == 4 || i == 10) {
            XSetForeground(display, gc, accent);
        } else {
            XSetForeground(display, gc, foreground);
        }
        XDrawString(display, window, gc, left + 30, y, lines[i],
                    (int)strlen(lines[i]));
        y += 23;
    }
    XSetForeground(display, gc, subdued);
    XDrawString(display, window, gc, left + 30, top + HELP_HEIGHT - 20,
                "Help is available from the desktop icon or Settings (H).", 56);
}

static void draw_resolution_confirmation(Display *display, Window window,
                                         GC gc, int width, int height,
                                         unsigned long foreground,
                                         unsigned long accent,
                                         unsigned long panel, int seconds)
{
    int left = (width - CONFIRM_WIDTH) / 2;
    int top = (height - CONFIRM_HEIGHT) / 2;
    XSetForeground(display, gc, panel);
    XFillRectangle(display, window, gc, left, top, CONFIRM_WIDTH, CONFIRM_HEIGHT);
    XSetForeground(display, gc, accent);
    XFillRectangle(display, window, gc, left, top, 4, CONFIRM_HEIGHT);
    XSetForeground(display, gc, foreground);
    char question[128];
    snprintf(question, sizeof(question),
             "Keep %d x %d? Reverting in %d seconds.",
             supported_resolutions[selected_resolution].width,
             supported_resolutions[selected_resolution].height, seconds);
    XDrawString(display, window, gc, left + 24, top + 36,
                question, (int)strlen(question));
    XSetForeground(display, gc, accent);
    XDrawString(display, window, gc, left + 45, top + 82,
                "[ Keep ]", 8);
    XSetForeground(display, gc, foreground);
    XDrawString(display, window, gc, left + 300, top + 82,
                "[ Revert ]", 10);
}

static void open_terminal(int selection)
{
    const TerminalOption *terminal =
        &terminal_options[available_terminals[selection]];
    pid_t child = fork();
    if (child == 0) {
        if (terminal->option != NULL) {
            execlp(terminal->name, terminal->name, terminal->option,
                   (char *)NULL);
        } else {
            execlp(terminal->name, terminal->name, (char *)NULL);
        }
        perror("Could not start terminal");
        _exit(127);
    }

    if (child < 0) {
        perror("Could not start terminal");
        return;
    }

    (void)child;
}

int main(int argc, char **argv)
{
    int own_session = argc > 1 && strcmp(argv[1], "--x-session") == 0;
    const char *display_environment = getenv("DISPLAY");
    if (!own_session &&
        ((display_environment == NULL || display_environment[0] == '\0') ||
         running_on_virtual_console())) {
        start_x_server("--x-session");
    }

    pid_t window_manager = own_session ? start_window_manager() : -1;
    if (own_session && window_manager < 0) {
        return 1;
    }

    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Could not connect to the X11 display.\n");
        if (window_manager > 0) {
            kill(window_manager, SIGTERM);
            while (waitpid(window_manager, NULL, 0) < 0 && errno == EINTR) {
            }
        }
        return 1;
    }
    signal(SIGCHLD, SIG_DFL);

    int screen = DefaultScreen(display);
    Cursor pointer_cursor = XCreateFontCursor(display, XC_left_ptr);
    XDefineCursor(display, RootWindow(display, screen), pointer_cursor);
    XFreeCursor(display, pointer_cursor);
    int width = DisplayWidth(display, screen);
    int height = DisplayHeight(display, screen);
    appearance_init(&appearance);
    animations_init(&animations);
    if (!desktop_items_init(&desktop_items, MAX_DESKTOP_ITEMS)) {
        memset(&desktop_items, 0, sizeof(desktop_items));
        desktop_items.watch_fd = -1;
        desktop_items.watch_id = -1;
    }
    wallpaper_candidate_count = appearance_images_load(
        wallpaper_candidates, sizeof(wallpaper_candidates) /
                              sizeof(wallpaper_candidates[0]));
    unsigned long background;
    unsigned long foreground;
    unsigned long accent;
    unsigned long panel;
    unsigned long glow;
    set_appearance_palette(display, screen, &background, &foreground,
                           &accent, &panel, &glow);
    unsigned long subdued = color(display, screen, "#9aaea2");
    if (appearance.image_enabled) {
        wallpaper_applied = appearance_apply_wallpaper(&appearance);
    }

    XSetWindowAttributes attributes;
    attributes.override_redirect = True;
    attributes.background_pixel = background;

    Window window = XCreateWindow(
        display, RootWindow(display, screen), 0, 0,
        (unsigned int)width, (unsigned int)height, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel, &attributes);
    XSelectInput(display, window, ExposureMask | ButtonPressMask | KeyPressMask);

    GC gc = XCreateGC(display, window, 0, NULL);
    XFontStruct *font = XLoadQueryFont(display, "fixed");
    if (font != NULL) {
        XSetFont(display, gc, font->fid);
    }

    XMapWindow(display, window);
    XLowerWindow(display, window);
    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XFlush(display);

    Atom command_atom = XInternAtom(display, "_FIELD_DESKTOP_COMMAND", False);
    Atom owner_atom = XInternAtom(display, "_FIELD_DESKTOP_COMMAND_OWNER", False);
    Atom wallpaper_atom = XInternAtom(display, "_XROOTPMAP_ID", False);
    XSetSelectionOwner(display, owner_atom, window, CurrentTime);
    performance_stats_enabled = load_performance_stats();
    tray_initialize(display, RootWindow(display, screen), gc, foreground, panel);
    if (!startup_apps_init(&startup_apps, MAX_APPLICATIONS)) {
        fprintf(stderr, "Could not load user startup applications.\n");
    } else if (startup_apps.enabled &&
               !startup_apps_launch_enabled(&startup_apps)) {
        fprintf(stderr, "One or more user startup applications could not launch.\n");
    }
    refresh_supported_resolutions(display, RootWindow(display, screen));
    if (randr_configuration != NULL) {
        XRRSelectInput(display, RootWindow(display, screen),
                       RRScreenChangeNotifyMask);
    }
    XSelectInput(display, RootWindow(display, screen),
                 StructureNotifyMask | PropertyChangeMask);

    for (int i = 0; i < (int)(sizeof(terminal_options) / sizeof(terminal_options[0])); ++i) {
        if (find_executable(terminal_options[i].name)) {
            available_terminals[terminal_count++] = i;
        }
    }
    application_count = applications_load(applications, MAX_APPLICATIONS);
    int selected_terminal = terminal_count > 0 ? load_terminal_selection() : -1;
    tray_update_monitor(display, gc, foreground, panel, window_manager);

    int settings_view = 0;
    Window launcher_window = None;
    Window previous_focus = None;
    int launcher_selected = 0;
    char launcher_query[128] = "";
    int previous_desktop_click = -1;
    Time previous_desktop_click_time = 0;
    time_t last_monitor_update = time(NULL);
    int feh_available = appearance_feh_available();
    XEvent event;

    for (;;) {
        reap_child_processes(&window_manager);
        time_t now = time(NULL);
        if (resolution_confirmation) {
            int remaining = (int)(confirmation_deadline - now);
            if (remaining != confirmation_seconds) {
                confirmation_seconds = remaining;
                if (settings_view || resolution_confirmation) {
                    XClearArea(display, window, 0, 0, 0, 0, True);
                }
            }
            if (confirmation_seconds <= 0) {
                finish_resolution_confirmation(display,
                                               RootWindow(display, screen), 0);
                update_desktop_geometry(display, window, gc, foreground,
                                        panel, &width, &height);
                XClearArea(display, window, 0, 0, 0, 0, True);
            }
        }
        if (now - last_monitor_update >= 1) {
            tray_update_monitor(display, gc, foreground, panel,
                                window_manager);
            int feh_now_available = appearance_feh_available();
            if (feh_now_available != feh_available) {
                feh_available = feh_now_available;
                if (settings_view &&
                    settings_page == SETTINGS_PAGE_APPEARANCE) {
                    XClearArea(display, window, 0, 0, 0, 0, True);
                }
            }
            XFlush(display);
            last_monitor_update = now;
        }
        while (XPending(display) == 0) {
            struct pollfd poll_fds[2] = {
                {
                    .fd = ConnectionNumber(display),
                    .events = POLLIN,
                    .revents = 0
                },
                {
                    .fd = desktop_items.watch_fd,
                    .events = POLLIN,
                    .revents = 0
                }
            };
            nfds_t poll_count = desktop_items.watch_fd >= 0 ? 2 : 1;
            int ready = poll(poll_fds, poll_count, 1000);
            if (ready == 0) {
                tray_update_monitor(display, gc, foreground, panel,
                                    window_manager);
                if (resolution_confirmation) {
                    int remaining = (int)(confirmation_deadline - time(NULL));
                    if (remaining != confirmation_seconds) {
                        confirmation_seconds = remaining;
                        XClearArea(display, window, 0, 0, 0, 0, True);
                    }
                    if (remaining <= 0) {
                        finish_resolution_confirmation(
                            display, RootWindow(display, screen), 0);
                        update_desktop_geometry(display, window, gc,
                                                foreground, panel,
                                                &width, &height);
                        XClearArea(display, window, 0, 0, 0, 0, True);
                    }
                }
                XFlush(display);
                last_monitor_update = time(NULL);
            } else if (ready < 0 && errno != EINTR) {
                perror("Could not poll X11 events");
                goto cleanup;
            } else if (ready > 0 && poll_count == 2 &&
                       (poll_fds[1].revents & POLLIN) != 0) {
                if (desktop_items_handle_changes(&desktop_items)) {
                    selected_desktop_item = -1;
                    XClearArea(display, window, 0, 0, 0, 0, True);
                }
            }
            if (ready > 0 &&
                (poll_fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
                fprintf(stderr, "X11 display connection was closed.\n");
                goto cleanup;
            }
        }
        XNextEvent(display, &event);
        if (randr_configuration != NULL &&
            event.type == randr_event_base + RRScreenChangeNotify) {
            XRRUpdateConfiguration(&event);
            refresh_supported_resolutions(display, RootWindow(display, screen));
            if (supported_resolution_count == 0) {
                resolution_confirmation = 0;
            }
            update_desktop_geometry(display, window, gc, foreground,
                                    panel, &width, &height);
        } else if (event.type == ConfigureNotify &&
                   event.xconfigure.window == RootWindow(display, screen)) {
            update_desktop_geometry(display, window, gc, foreground,
                                    panel, &width, &height);
        } else if (event.type == ClientMessage &&
            event.xclient.message_type == command_atom &&
            event.xclient.data.l[0] == 1) {
            if (launcher_window == None) {
                int revert_to;
                XGetInputFocus(display, &previous_focus, &revert_to);
                launcher_query[0] = '\0';
                launcher_selected = 0;
            }
            settings_view = 0;
            help_view = 0;
            launcher_window = open_launcher(
                display, RootWindow(display, screen), launcher_window, gc,
                foreground, accent, panel, subdued, launcher_query,
                launcher_selected, &animations);
        } else if (event.type == ClientMessage &&
                   event.xclient.message_type == system_tray.opcode &&
                   event.xclient.data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
            tray_dock_icon(display, (Window)event.xclient.data.l[2],
                           gc, foreground, panel);
        } else if (event.type == Expose &&
                   event.xexpose.window == system_tray.window &&
                   event.xexpose.count == 0) {
            XSetForeground(display, gc, panel);
            XFillRectangle(display, system_tray.window, gc, 0, 0,
                           (unsigned int)(DisplayWidth(display, screen)),
                           TRAY_ICON_SIZE + TRAY_ICON_GAP * 2);
        } else if (event.type == Expose &&
                   event.xexpose.window == system_tray.status_window &&
                   event.xexpose.count == 0) {
            tray_status_redraw(display, gc, foreground, panel);
        } else if (event.type == ConfigureRequest &&
                   event.xconfigurerequest.parent == system_tray.window) {
            int index = tray_icon_index(event.xconfigurerequest.window);
            if (index >= 0) {
                XMoveResizeWindow(display, system_tray.icons[index].window,
                                  0, TRAY_ICON_GAP, TRAY_ICON_SIZE,
                                  TRAY_ICON_SIZE);
            }
        } else if (event.type == DestroyNotify) {
            tray_remove_icon(display, event.xdestroywindow.window,
                             gc, foreground, panel);
        } else if (event.type == PropertyNotify &&
                   event.xproperty.window == RootWindow(display, screen) &&
                   event.xproperty.atom == wallpaper_atom) {
            XClearArea(display, window, 0, 0, 0, 0, True);
        } else if (event.type == PropertyNotify &&
                   event.xproperty.atom == system_tray.xembed_info) {
            int index = tray_icon_index(event.xproperty.window);
            if (index >= 0) {
                system_tray.icons[index].mapped =
                    tray_icon_requests_mapping(display,
                                               system_tray.icons[index].window);
                if (system_tray.icons[index].mapped) {
                    XMapWindow(display, system_tray.icons[index].window);
                } else {
                    XUnmapWindow(display, system_tray.icons[index].window);
                }
                tray_redraw(display, gc, foreground, panel);
            }
        } else if ((event.type == MapNotify || event.type == UnmapNotify) &&
                   event.xany.window != system_tray.window) {
            int index = tray_icon_index(event.xany.window);
            if (index >= 0) {
                system_tray.icons[index].mapped =
                    tray_icon_requests_mapping(display,
                                               system_tray.icons[index].window);
                tray_redraw(display, gc, foreground, panel);
            }
        } else if (event.type == Expose && event.xexpose.count == 0 &&
                   event.xexpose.window == launcher_window) {
            draw_launcher(display, launcher_window, gc, foreground, accent,
                          panel, subdued, launcher_query, launcher_selected);
        } else if (event.type == Expose && event.xexpose.count == 0 &&
                   event.xexpose.window == window) {
            draw_desktop(display, window, gc, width, height,
                         background, foreground, accent,
                         panel, glow, &desktop_items, selected_desktop_item);
            if (settings_view) {
                if (settings_page == SETTINGS_PAGE_APPEARANCE) {
                    draw_appearance_settings(display, window, gc, width, height,
                                             background, foreground, accent,
                                             panel, subdued);
                } else if (settings_page == SETTINGS_PAGE_OPTIONS) {
                    draw_optional_settings(display, window, gc, width, height,
                                           foreground, accent, panel, subdued);
                } else {
                    draw_terminal_settings(display, window, gc, width, height,
                                           foreground, accent, panel, subdued,
                                           selected_terminal,
                                           performance_stats_enabled,
                                           selected_resolution,
                                           resolution_scroll,
                                           resolution_confirmation
                                               ? confirmation_seconds : 0);
                }
            } else if (help_view) {
                draw_help(display, window, gc, width, height,
                          foreground, accent, panel, subdued);
            }
            if (resolution_confirmation && selected_resolution >= 0 &&
                selected_resolution < supported_resolution_count) {
                draw_resolution_confirmation(display, window, gc,
                                             width, height, foreground,
                                             accent, panel,
                                             confirmation_seconds);
            }
        } else if (event.type == ButtonPress) {
            int x = event.xbutton.x;
            int y = event.xbutton.y;
            if (event.xbutton.window == launcher_window &&
                launcher_window != None) {
                int row = (y - 124) / 27;
                int first = launcher_selected >= LAUNCHER_ROWS
                    ? launcher_selected - LAUNCHER_ROWS + 1 : 0;
                int result_count = matching_application_count(launcher_query);
                if (y >= 107 && y < LAUNCHER_HEIGHT - 25 &&
                    row >= 0 && row < LAUNCHER_ROWS && first + row < result_count) {
                    launcher_selected = first + row;
                    int application_index = matching_application_at(
                        launcher_query, launcher_selected);
                    if (application_index >= 0) {
                        if (application_launch(&applications[application_index])) {
                            XDestroyWindow(display, launcher_window);
                            launcher_window = None;
                        }
                    }
                }
            } else if (event.xbutton.window == window &&
                       resolution_confirmation) {
                int left = (width - CONFIRM_WIDTH) / 2;
                int top = (height - CONFIRM_HEIGHT) / 2;
                if (y >= top + 55 && y <= top + 105 &&
                    x >= left + 20 && x <= left + CONFIRM_WIDTH / 2) {
                    finish_resolution_confirmation(display,
                                                   RootWindow(display, screen), 1);
                    XClearArea(display, window, 0, 0, 0, 0, True);
                } else if (y >= top + 55 && y <= top + 105 &&
                           x > left + CONFIRM_WIDTH / 2 &&
                           x <= left + CONFIRM_WIDTH - 20) {
                    finish_resolution_confirmation(display,
                                                   RootWindow(display, screen), 0);
                    update_desktop_geometry(display, window, gc, foreground,
                                            panel, &width, &height);
                    XClearArea(display, window, 0, 0, 0, 0, True);
                }
            } else if (event.xbutton.window == window && settings_view) {
                int left = (width - SETTINGS_WIDTH) / 2;
                int top = (height - SETTINGS_HEIGHT) / 2;
                if (y >= top + 50 && y <= top + 80) {
                    if (x >= left + 20 && x < left + 145) {
                        settings_page = SETTINGS_PAGE_GENERAL;
                    } else if (x >= left + 145 && x < left + 285) {
                        settings_page = SETTINGS_PAGE_APPEARANCE;
                    } else if (x >= left + 285 && x < left + 390) {
                        settings_page = SETTINGS_PAGE_OPTIONS;
                    }
                    XClearArea(display, window, 0, 0, 0, 0, True);
                } else if (settings_page == SETTINGS_PAGE_APPEARANCE) {
                    int appearance_changed = 0;
                    if (y >= top + 88 && y <= top + 118 &&
                        x >= left + 24 && x <= left + 600) {
                        int next_theme = (appearance.theme + 1) %
                            (int)appearance_theme_count();
                        appearance_changed =
                            appearance_select_theme(&appearance, next_theme);
                    } else if (y >= top + 120 && y <= top + 155 &&
                               x >= left + 24 && x <= left + 610) {
                        int next_background = (appearance.background + 1) %
                            (int)appearance_background_count();
                        appearance_changed =
                            appearance_select_background(&appearance,
                                                         next_background);
                    } else if (y >= top + 164 && y <= top + 195 &&
                               x >= left + 24 && x <= left + 610) {
                        int enabled = !appearance.image_enabled;
                        if (appearance_set_image_enabled(&appearance, enabled)) {
                            if (enabled) {
                                wallpaper_applied =
                                    appearance_apply_wallpaper(&appearance);
                            } else {
                                wallpaper_applied = 0;
                                appearance_clear_wallpaper(display);
                            }
                            appearance_changed = 1;
                        }
                    } else if (y >= top + 214 && y <= top + 252 &&
                               x >= left + 24 && x <= left + 610) {
                        if (!appearance_feh_available()) {
                            (void)appearance_install_feh();
                        } else if (appearance.image_enabled &&
                                   !wallpaper_applied) {
                            wallpaper_applied =
                                appearance_apply_wallpaper(&appearance);
                            XClearArea(display, window, 0, 0, 0, 0, True);
                        }
                    } else if (y >= top + 282 &&
                               y < top + 282 + APPEARANCE_IMAGE_ROWS * 22 &&
                               x >= left + 24 && x <= left + 610) {
                        int row = (y - (top + 282)) / 22;
                        size_t image_index = wallpaper_scroll + (size_t)row;
                        if (row >= 0 && row < APPEARANCE_IMAGE_ROWS &&
                            image_index < wallpaper_candidate_count &&
                            appearance_select_image(&appearance,
                                            wallpaper_candidates[image_index])) {
                            if (appearance.image_enabled) {
                                wallpaper_applied =
                                    appearance_apply_wallpaper(&appearance);
                            }
                            appearance_changed = 1;
                        }
                    }
                    if (appearance_changed) {
                        set_appearance_palette(display, screen, &background,
                                               &foreground, &accent, &panel, &glow);
                        subdued = color(display, screen, "#9aaea2");
                        XSetWindowBackground(display, window, background);
                        tray_redraw(display, gc, foreground, panel);
                        tray_update_monitor(display, gc, foreground, panel,
                                            window_manager);
                    }
                    XClearArea(display, window, 0, 0, 0, 0, True);
                } else if (settings_page == SETTINGS_PAGE_OPTIONS) {
                    if (y >= top + 105 && y <= top + 140 &&
                        x >= left + 24 && x <= left + 610) {
                        (void)startup_apps_set_enabled(&startup_apps,
                                                       !startup_apps.enabled);
                    } else if (y >= top + 215 && y <= top + 252 &&
                               x >= left + 24 && x <= left + 610) {
                        (void)animations_set_enabled(&animations,
                                                     !animations.enabled);
                    }
                    XClearArea(display, window, 0, 0, 0, 0, True);
                } else {
                    if (y >= top + 250 && y <= top + 290 &&
                        x >= left + 24 && x <= left + 280) {
                        performance_stats_enabled = !performance_stats_enabled;
                        resource_monitor.initialized = 0;
                        resource_monitor.manager_initialized = 0;
                        resource_monitor.desktop_cpu = 0.0;
                        resource_monitor.manager_cpu = 0.0;
                        save_performance_stats(performance_stats_enabled);
                        tray_update_monitor(display, gc, foreground, panel,
                                            window_manager);
                    } else if (y >= top + 110 && y <= top + 160 &&
                               (y - (top + 110)) / 30 < terminal_count &&
                        x >= left + 24 && x <= left + 269) {
                        selected_terminal = (y - (top + 110)) / 30;
                        save_terminal_selection(selected_terminal);
                    } else if (x >= left + 305 && x <= left + 610 &&
                               y >= top + 130 &&
                               y < top + 142 + RESOLUTION_ROWS * 27) {
                        int selected_row = (y - (top + 130)) / 27;
                        int mode = resolution_scroll + selected_row;
                        if (selected_row >= 0 && selected_row < RESOLUTION_ROWS &&
                            mode < supported_resolution_count) {
                            start_resolution_confirmation(
                                display, RootWindow(display, screen), mode);
                            XClearArea(display, window, 0, 0, 0, 0, True);
                        }
                    } else if (x >= left + 24 && x <= left + 280 &&
                               y >= top + SETTINGS_HEIGHT - 60) {
                        settings_view = 0;
                        help_view = 1;
                    }
                    XClearArea(display, window, 0, 0, 0, 0, True);
                }
            } else if (event.xbutton.window == window && help_view) {
                help_view = 0;
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (event.xbutton.window == window) {
                int action = -1;
                const int icon_x[] = {34, 150, 266, 34, 150, 266};
                const int icon_y[] = {84, 84, 84, 180, 180, 180};
                for (int i = 0; i < 6; ++i) {
                    if (x >= icon_x[i] - 10 && x <= icon_x[i] + 94 &&
                        y >= icon_y[i] - 8 && y <= icon_y[i] + 78) {
                        action = i;
                        break;
                    }
                }
                if (action == 0) {
                    if (launcher_window == None) {
                        int revert_to;
                        XGetInputFocus(display, &previous_focus, &revert_to);
                    }
                    launcher_query[0] = '\0';
                    launcher_selected = 0;
                    launcher_window = open_launcher(
                        display, RootWindow(display, screen), launcher_window, gc,
                        foreground, accent, panel, subdued, launcher_query,
                        launcher_selected, &animations);
                } else if (action == 1 && terminal_count > 0) {
                    open_terminal(selected_terminal);
                } else if (action == 2) {
                    applications_launch_kind(applications, application_count,
                                             APPLICATION_FILE_MANAGER);
                } else if (action == 3) {
                    applications_launch_kind(applications, application_count,
                                             APPLICATION_BROWSER);
                } else if (action == 4) {
                    help_view = 0;
                    settings_view = 1;
                    settings_page = SETTINGS_PAGE_GENERAL;
                    XClearArea(display, window, 0, 0, 0, 0, True);
                } else if (action == 5) {
                    settings_view = 0;
                    help_view = 1;
                    XClearArea(display, window, 0, 0, 0, 0, True);
                }
                if (action < 0 && desktop_items.count > 0 && x >= 24 &&
                    y >= 270 && y < height - 90) {
                    int column = (x - 24) / 116;
                    int row = (y - 270) / 94;
                    int index = row * DESKTOP_ITEM_COLUMNS + column;
                    if (column >= 0 && column < DESKTOP_ITEM_COLUMNS &&
                        row >= 0 && row < desktop_item_visible_rows(height) &&
                        (x - 24) % 116 < 104 && (y - 270) % 94 < 82 &&
                        index >= 0 && (size_t)index < desktop_items.count) {
                        if (selected_desktop_item == index &&
                            event.xbutton.time - previous_desktop_click_time <= 400) {
                            const DesktopItem *item =
                                desktop_items_get(&desktop_items, (size_t)index);
                            (void)desktop_item_open(item);
                            selected_desktop_item = -1;
                        } else {
                            selected_desktop_item = index;
                        }
                        previous_desktop_click = index;
                        previous_desktop_click_time = event.xbutton.time;
                        XClearArea(display, window, 0, 0, 0, 0, True);
                    } else if (previous_desktop_click != index) {
                        selected_desktop_item = -1;
                        previous_desktop_click = -1;
                        XClearArea(display, window, 0, 0, 0, 0, True);
                    }
                }
            }
        } else if (event.type == KeyPress) {
            KeySym key = XLookupKeysym(&event.xkey, 0);
            if (event.xkey.window == launcher_window &&
                launcher_window != None) {
                char text[16];
                KeySym translated;
                int length = XLookupString(&event.xkey, text, sizeof(text) - 1,
                                           &translated, NULL);
                int result_count = matching_application_count(launcher_query);
                if (translated == XK_Escape) {
                    XDestroyWindow(display, launcher_window);
                    launcher_window = None;
                    if (previous_focus != None) {
                        XSetInputFocus(display, previous_focus, RevertToParent,
                                       CurrentTime);
                    }
                } else if (translated == XK_Return || translated == XK_KP_Enter) {
                    int application_index = matching_application_at(
                        launcher_query, launcher_selected);
                    if (application_index >= 0) {
                        if (application_launch(&applications[application_index])) {
                            XDestroyWindow(display, launcher_window);
                            launcher_window = None;
                        }
                    }
                } else if (translated == XK_Up && result_count > 0) {
                    launcher_selected = (launcher_selected + result_count - 1) %
                                        result_count;
                    XClearArea(display, launcher_window, 0, 0, 0, 0, True);
                } else if (translated == XK_Down && result_count > 0) {
                    launcher_selected = (launcher_selected + 1) % result_count;
                    XClearArea(display, launcher_window, 0, 0, 0, 0, True);
                } else if (translated == XK_BackSpace) {
                    size_t query_length = strlen(launcher_query);
                    if (query_length > 0) {
                        launcher_query[query_length - 1] = '\0';
                        launcher_selected = 0;
                        XClearArea(display, launcher_window, 0, 0, 0, 0, True);
                    }
                } else if (length > 0 && isprint((unsigned char)text[0]) &&
                           strlen(launcher_query) + (size_t)length <
                               sizeof(launcher_query)) {
                    text[length] = '\0';
                    strncat(launcher_query, text,
                            sizeof(launcher_query) - strlen(launcher_query) - 1);
                    launcher_selected = 0;
                    XClearArea(display, launcher_window, 0, 0, 0, 0, True);
                }
            } else if (resolution_confirmation &&
                       (key == XK_Return || key == XK_KP_Enter)) {
                finish_resolution_confirmation(display,
                                               RootWindow(display, screen), 1);
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (resolution_confirmation && key == XK_Escape) {
                finish_resolution_confirmation(display,
                                               RootWindow(display, screen), 0);
                update_desktop_geometry(display, window, gc, foreground,
                                        panel, &width, &height);
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (help_view && key == XK_Escape) {
                help_view = 0;
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (key == XK_Escape) {
                if (settings_view) {
                    settings_view = 0;
                    XClearArea(display, window, 0, 0, 0, 0, True);
                } else {
                    break;
                }
            } else if (settings_view && settings_page == SETTINGS_PAGE_GENERAL &&
                       terminal_count > 0 &&
                       (key == XK_Up || key == XK_Down)) {
                selected_terminal = (selected_terminal +
                    (key == XK_Down ? 1 : terminal_count - 1)) % terminal_count;
                save_terminal_selection(selected_terminal);
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (settings_view && settings_page == SETTINGS_PAGE_GENERAL &&
                       key == XK_p) {
                performance_stats_enabled = !performance_stats_enabled;
                resource_monitor.initialized = 0;
                resource_monitor.manager_initialized = 0;
                resource_monitor.desktop_cpu = 0.0;
                resource_monitor.manager_cpu = 0.0;
                save_performance_stats(performance_stats_enabled);
                tray_update_monitor(display, gc, foreground, panel,
                                    window_manager);
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (settings_view && settings_page == SETTINGS_PAGE_GENERAL &&
                       key == XK_h) {
                settings_view = 0;
                help_view = 1;
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (settings_view && settings_page == SETTINGS_PAGE_GENERAL &&
                       (key == XK_Left || key == XK_Right) &&
                       supported_resolution_count > 0) {
                int delta = key == XK_Left ? -1 : 1;
                selected_resolution =
                    (selected_resolution + delta + supported_resolution_count) %
                    supported_resolution_count;
                if (selected_resolution < resolution_scroll) {
                    resolution_scroll = selected_resolution;
                } else if (selected_resolution >=
                           resolution_scroll + RESOLUTION_ROWS) {
                    resolution_scroll =
                        selected_resolution - RESOLUTION_ROWS + 1;
                }
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (settings_view && settings_page == SETTINGS_PAGE_GENERAL &&
                       (key == XK_Page_Up || key == XK_Page_Down)) {
                int direction = key == XK_Page_Up ? -RESOLUTION_ROWS :
                                                     RESOLUTION_ROWS;
                int maximum = supported_resolution_count > RESOLUTION_ROWS
                    ? supported_resolution_count - RESOLUTION_ROWS : 0;
                resolution_scroll += direction;
                if (resolution_scroll < 0) {
                    resolution_scroll = 0;
                } else if (resolution_scroll > maximum) {
                    resolution_scroll = maximum;
                }
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (settings_view && settings_page == SETTINGS_PAGE_GENERAL &&
                       (key == XK_Return || key == XK_KP_Enter)) {
                start_resolution_confirmation(display,
                                               RootWindow(display, screen),
                                               selected_resolution);
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (settings_view && settings_page == SETTINGS_PAGE_OPTIONS &&
                       (key == XK_s || key == XK_S)) {
                (void)startup_apps_set_enabled(&startup_apps,
                                               !startup_apps.enabled);
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (settings_view && settings_page == SETTINGS_PAGE_OPTIONS &&
                       (key == XK_a || key == XK_A)) {
                (void)animations_set_enabled(&animations,
                                             !animations.enabled);
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (settings_view &&
                       settings_page == SETTINGS_PAGE_APPEARANCE &&
                       (key == XK_Page_Up || key == XK_Page_Down)) {
                size_t maximum = wallpaper_candidate_count >
                    APPEARANCE_IMAGE_ROWS
                    ? wallpaper_candidate_count - APPEARANCE_IMAGE_ROWS : 0;
                if (key == XK_Page_Up) {
                    wallpaper_scroll = wallpaper_scroll > APPEARANCE_IMAGE_ROWS
                        ? wallpaper_scroll - APPEARANCE_IMAGE_ROWS : 0;
                } else {
                    wallpaper_scroll += APPEARANCE_IMAGE_ROWS;
                    if (wallpaper_scroll > maximum) {
                        wallpaper_scroll = maximum;
                    }
                }
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (settings_view &&
                       (key == XK_1 || key == XK_2 || key == XK_3)) {
                settings_page = (int)(key - XK_1);
                XClearArea(display, window, 0, 0, 0, 0, True);
            } else if (event.xkey.window == window &&
                       (event.xkey.state & ControlMask) != 0 &&
                       key == XK_space) {
                if (launcher_window == None) {
                    int revert_to;
                    XGetInputFocus(display, &previous_focus, &revert_to);
                }
                launcher_query[0] = '\0';
                launcher_selected = 0;
                launcher_window = open_launcher(
                    display, RootWindow(display, screen), launcher_window, gc,
                    foreground, accent, panel, subdued, launcher_query,
                    launcher_selected, &animations);
            }
        }
    }

cleanup:
    if (launcher_window != None) {
        XDestroyWindow(display, launcher_window);
    }
    desktop_items_destroy(&desktop_items);
    startup_apps_destroy(&startup_apps);
    tray_shutdown(display);
    if (randr_configuration != NULL) {
        XRRFreeScreenConfigInfo(randr_configuration);
    }
    if (window_manager > 0) {
        kill(window_manager, SIGTERM);
        while (waitpid(window_manager, NULL, 0) < 0 && errno == EINTR) {
        }
    }
    if (font != NULL) {
        XFreeFont(display, font);
    }
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    return 0;
}