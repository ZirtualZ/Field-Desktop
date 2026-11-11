# Field Desktop

Field Desktop is a lightweight desktop environment for Linux and X11. Its
desktop shell uses GTK 4, paired with a small custom floating window manager.
The interface follows a familiar panel-and-taskbar layout with a searchable
application launcher and focused settings.

## Features

- GTK desktop with an application panel, common shortcuts, and Desktop-folder
  items.
- Searchable application launcher, including the window manager's `Alt+Space`
  shortcut.
- Separate floating window manager with taskbar, switcher, move/resize
  controls, and keyboard shortcuts.
- Per-monitor desktop surfaces and taskbars, with the current primary display
  indicated in the panel.
- Settings for color themes, solid backgrounds, image wallpaper, desktop
  items, panel, and taskbar.

Field Desktop launches installed applications rather than bundling a browser,
file manager, or terminal. The project targets Linux and X11.

## Requirements

Build requirements:

- A C11 compiler and `make`.
- GTK 4.12 or newer with its X11 backend.
- Xlib, XRandR, and Xft development libraries.
- `pkg-config`.

To run a complete session, install an X server and a terminal emulator. A file
manager and browser are optional; installed applications are used when
available.

Example packages:

```sh
# Arch Linux
sudo pacman -S --needed base-devel pkgconf gtk4 libx11 libxrandr libxft \
  xorg-server xorg-xinit xfce4-terminal

# Debian and Ubuntu
sudo apt install build-essential pkg-config libgtk-4-dev libx11-dev \
  libxrandr-dev libxft-dev xserver-xorg xinit xfce4-terminal
```

## Build

From the repository root:

```sh
make
```

This creates `simple-desktop` and `simple-wm` in the repository root. To remove
the generated binaries:

```sh
make clean
```

Run the focused application-entry check with:

```sh
make check-applications
```

## Run

For a complete session from a text console (TTY), log in and run:

```sh
./scripts/run-tty3.sh
```

The script locates the project regardless of the current working directory.
The desktop starts X on a free display when one is not already available, then
starts its custom window manager.

You can also run `./simple-desktop` directly from a TTY. When started inside an
existing graphical session, Field Desktop uses that session's window manager;
it cannot take over a display already managed by another window manager.
Press **Escape** to exit the desktop session.

## Install as a login session

Install the binaries and X session entry:

```sh
sudo make install
```

A display manager that reads `/usr/share/xsessions` will then offer **Field
Desktop** in its session chooser. Log out, choose Field Desktop, and sign in.
The session uses the display manager's X display; it does not start a nested X
server.

The install paths can be customized with `PREFIX`, `BINDIR`, `XSESSIONDIR`, and
`DESTDIR`. For example:

```sh
make install DESTDIR=/tmp/package-root
```

## Controls

### Application Launcher

| Action | Control |
| --- | --- |
| Open the application launcher | `Alt+Space` or click **Applications** |
| Filter apps / navigate results / launch | Type / `Up` or `Down` / `Enter` |
| Close the launcher | `Escape` |

### Window Management

| Action | Control |
| --- | --- |
| Cycle windows / open the switcher | `Alt+Tab` |
| Cycle backward | `Alt+Shift+Tab` |
| Dismiss the switcher | `Alt+Escape` |
| Close the focused window | `Alt+F4` |
| Move a window | Drag its title bar, or `Alt` + left-drag |
| Resize a window | Drag its bottom/right edge/corner; or `Alt` + right-drag |

### Notes

- The launcher reads visible application entries from these locations:
  - `~/.local/share/applications`
  - `$XDG_DATA_HOME/applications`
  - `applications` directories under `$XDG_DATA_DIRS` (defaulting to `/usr/local/share:/usr/share`)
- Application commands are started directly, without a shell.
- Applications marked `Terminal=true` are opened in a supported installed terminal emulator.
- The Terminal shortcut prefers installed applications categorized as `TerminalEmulator`.
- Arbitrary executable files on the desktop are not run directly.

## Settings

### Appearance Page

Select a built-in color theme, solid background, image wallpaper, and installed icon pack.

- **Icon packs** are discovered from user and system icon directories. After installing a theme package, choose **Find installed icon packs** to refresh the list.
- Common Arch packages: `adwaita-icon-theme`, `papirus-icon-theme`, and `breeze-icons`.

### Desktop Page

Toggle the top panel, Desktop-folder items, and the window manager's taskbar. Changes apply immediately and are saved under `~/.config/simple-desktop/`.

### Display Notes

Each active display gets its own desktop surface, top panel, and taskbar. The window manager handles window placement, task switching, and taskbar rendering; the GTK shell owns the desktop, launcher, and settings surfaces.

## Project layout

```text
.
├── data/                 # X session entry
├── scripts/              # Optional TTY launcher
├── src/
│   ├── shell/            # GTK shell and X-session lifecycle
│   ├── applications.c    # Installed application discovery and launching
│   ├── appearance.c      # Theme and wallpaper preferences
│   ├── desktop_items.c   # Desktop-folder discovery and opening
│   └── wm.c              # Floating window manager
├── tests/                # Application-entry and X11 characterization tests
├── Makefile
└── LICENSE
```

## Improvement backlog

The following items are based on current implementation behavior and are
ordered by user impact. They are intended as actionable follow-up work, not
claims that the features already exist.

### High priority

- **Reap launched application processes.** `launch_arguments()` in
  `src/applications.c` forks and returns without waiting for or registering
  the child. Add a non-blocking child-reaping mechanism that works with the
  GTK event loop for application launches, path opens, and fallback launches.
  Verify that repeatedly starting short-lived commands does not leave zombie
  processes and that long-running applications remain independent.
- **Recover when the Desktop folder is created after startup.**
  `desktop_items_init()` treats a missing `~/Desktop` as a successful empty
  refresh, then cannot attach an inotify watch to that missing directory.
  Watch its parent or retry/recreate the watch so creating the folder and
  adding items during a session updates the desktop without a restart. Cover
  both initially missing folders and a folder removed and recreated later.
- **Handle the window-manager client limit safely.** `manage_window()` in
  `src/wm.c` returns without mapping a new window when `MAX_CLIENTS` (128) is
  reached, leaving the requested window invisible. Grow the managed-client
  storage or provide a deliberate overflow behavior that does not strand
  clients. Verify by opening more than 128 windows and closing them again.

### Medium priority

- **Keep newly mapped windows reachable.** `manage_window()` clamps initial
  positions only at the right and bottom edges; negative positions can leave a
  title bar off-screen. Clamp both axes to the selected monitor's usable
  bounds, while preserving intended placement where possible. Test windows
  requesting negative coordinates and layouts with monitors positioned left
  or above the primary display.
- **Honor `XDG_CONFIG_HOME` for preferences.** The feature and appearance
  settings paths in `src/shell/gtk_shell.c` and `src/appearance.c` are built
  directly from `HOME/.config`, even when `XDG_CONFIG_HOME` is set. Resolve
  the XDG config directory consistently (defaulting to `~/.config`) for both
  reads and writes. Verify custom-directory round trips and the default-path
  behavior.
- **Make collection limits visible or remove them.** Application discovery
  stops at 512 entries, Desktop-folder discovery at 256, monitor discovery
  at 16, and the window manager has a 128-client limit. Several callers
  silently truncate at capacity. Prefer dynamically sized collections where
  practical, or report a clear limit and expose the omitted-item count. Test
  each collection at and beyond its limit.
- **Improve `.desktop` entry compatibility.** `read_desktop_entry()` in
  `src/applications.c` handles a subset of Desktop Entry keys and values;
  for example, it does not apply `OnlyShowIn`/`NotShowIn` visibility rules or
  localized names, and the `Exec` field-code handling omits `%i`'s icon
  arguments. Implement the supported specification behavior deliberately,
  skip entries that should not be shown, and add fixture-based tests for
  visibility, localized names, quoting, and field codes.

### Testing and reliability

- **Expand fast automated tests for the core modules.**
  `make check-applications` now checks that `Terminal=true` metadata is
  retained, but application launch argument construction, preference
  persistence, Desktop-folder refresh behavior, and window-placement bounds
  still lack focused coverage. Add tests for these areas so core regressions
  can be checked without a full desktop session. The separate
  `tests/characterize.sh` X11 smoke/recording test depends on Xephyr, xdotool,
  xwininfo, and xterm and remains useful for end-to-end behavior.
- **Write preference files atomically.** `shell_save_settings()` and
  `appearance_save()` open the destination with `"w"`, truncating the previous
  settings before the new contents are complete. Write to a temporary file in
  the same directory and rename it into place only after a successful write;
  verify an interrupted or failed save leaves the previous preferences intact.

## License

Field Desktop is licensed under the [MIT License](LICENSE).
