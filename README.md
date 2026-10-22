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

| Action | Control |
| --- | --- |
| Open the application launcher | `Alt+Space` or click **Applications** |
| Filter apps / navigate results / launch | Type / `Up` or `Down` / `Enter` |
| Close the launcher | `Escape` |
| Open Settings | Click **Settings** in the panel or desktop |
| Launch a desktop shortcut or open a Desktop-folder item | Click its icon |
| Cycle windows / open the switcher | `Alt+Tab` |
| Cycle backward | `Alt+Shift+Tab` |
| Dismiss the switcher | `Alt+Escape` |
| Close the focused window | `Alt+F4` |
| Move a window | Drag its title bar, or `Alt` + left-drag |
| Resize a window | Drag its bottom/right edge or corner; or `Alt` + right-drag |

The launcher reads visible application entries from
`~/.local/share/applications`, `$XDG_DATA_HOME/applications`, and `applications`
directories under `$XDG_DATA_DIRS` (defaulting to `/usr/local/share:/usr/share`).
Application commands are started directly, without a shell. Arbitrary
executable files on the desktop are not run directly.

## Settings

Settings contains **Appearance** and **Desktop** pages.

- **Appearance** selects a built-in color theme, solid background, image
  wallpaper, and installed icon pack. Icon packs are discovered from user and
  system icon directories; after installing a theme package, choose **Find
  installed icon packs** to refresh the list. Common Arch packages include
  `adwaita-icon-theme`, `papirus-icon-theme`, and `breeze-icons`.
- **Desktop** toggles the top panel, Desktop-folder items, and the window
  manager's taskbar. Changes apply immediately and are saved under
  `~/.config/simple-desktop/`.

Each active display gets its own desktop surface, top panel, and taskbar. The
window manager handles window placement, task switching, and taskbar rendering;
the GTK shell owns the desktop, launcher, and settings surfaces.

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
├── tests/                # X11 characterization test and screenshots
├── Makefile
└── LICENSE
```

## License

Field Desktop is licensed under the [MIT License](LICENSE).
