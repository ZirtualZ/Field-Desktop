# Field Desktop

Field Desktop is an experimental, lightweight desktop environment for X11. It
combines a desktop shell with a small, custom floating window manager, written
in C.

## Features

- Desktop icons for files and folders in `~/Desktop`, plus an application
  launcher that searches installed desktop entries.
- A floating window manager with a taskbar, window switcher, move/resize
  controls, and keyboard shortcuts.
- A top panel with an XEmbed notification tray and system memory information.
- Settings for terminal selection, supported screen resolutions, colors, and
  wallpapers.
- Optional startup applications and launcher animations; both are off by
  default.

Field Desktop launches existing applications rather than bundling a browser,
file manager, or terminal. The project currently targets Linux and X11.

## Requirements

Build requirements:

- A C11 compiler and `make`.
- Xlib and XRandR development libraries.

To run a complete session, install an X server and a terminal emulator. A file
manager, browser, and `xdg-utils` are optional; the desktop uses installed
applications when available.

Example packages:

```sh
# Arch Linux
sudo pacman -S --needed base-devel libx11 libxrandr xorg-server xorg-xinit xfce4-terminal

# Debian and Ubuntu
sudo apt install build-essential libx11-dev libxrandr-dev xserver-xorg xinit xfce4-terminal
```

Resolution controls need XRandR support. `xdg-utils` provides the `xdg-open`
fallback for opening files.

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

### Launcher and desktop

| Action | Control |
| --- | --- |
| Open launcher anywhere in the custom session | `Alt+Space` |
| Open launcher when the desktop has focus | `Ctrl+Space` |
| Filter apps / navigate results / launch | Type / `Up` or `Down` / `Enter` |
| Close launcher | `Escape` |
| Open the in-session help | Click **Help** or press `h` in Settings |
| Open a file or folder | Double-click its desktop item |

The launcher reads visible application entries from
`~/.local/share/applications`, `$XDG_DATA_HOME/applications`, and `applications`
directories under `$XDG_DATA_DIRS` (defaulting to `/usr/local/share:/usr/share`).
Application commands are started directly, without a shell. Arbitrary executable
files on the desktop are not run directly.

### Window manager

| Action | Control |
| --- | --- |
| Cycle windows / open the switcher | `Alt+Tab` |
| Cycle backward | `Alt+Shift+Tab` |
| Dismiss the switcher | `Alt+Escape` |
| Close the focused window | `Alt+F4` |
| Move a window | Drag its title bar, or `Alt` + left-drag |
| Resize a window | Drag its bottom/right edge or corner; or `Alt` + right-drag |

Click a taskbar button or switcher entry to focus that window. If the taskbar
has more windows than fit, click the `+N more` area to open the window switcher.

## Settings and optional features

Settings has **General**, **Appearance**, and **More** pages.

- **General** selects the terminal and supported screen resolution. Test a mode
  with `Enter` or click it, then confirm within 15 seconds; otherwise the
  previous size is restored. If RandR is unavailable, resolution choices are
  disabled.
- **Appearance** offers four built-in themes, solid backgrounds, and optional
  image wallpapers discovered in `~/Pictures` and `~/Desktop`. Image wallpapers
  are off by default and require `feh`; choosing an image alone does not enable
  them. The optional graphical install action uses `pkexec` and Arch Linux's
  package manager.
- **More** contains optional XDG startup applications and launcher animations.
  Startup entries run on a later login after the setting is enabled.

Press `p` in Settings to toggle performance statistics. The panel always shows
system RAM used / total; when enabled, it also shows CPU and resident memory for
Field Desktop and its window manager. The preference is saved in
`~/.config/simple-desktop/performance`.

Other preferences are stored under `~/.config/simple-desktop/`: terminal,
appearance, startup-app, and animation settings. Only one XEmbed tray manager
can own a display; if another desktop environment's tray is running, its icons
remain with that manager.

## Project layout

```text
.
├── data/                 # X session entry
├── scripts/              # Optional TTY launcher
├── src/                  # Desktop, window manager, and feature modules
├── Makefile
├── LICENSE
└── README.md
```

## License

Field Desktop is licensed under the [MIT License](LICENSE).
