# Field Desktop

An experimental, lightweight X11 desktop with a custom icon shell and a small
handmade floating window manager. The WM is intentionally minimal: it frames
windows, focuses and raises them, closes them, and supports moving/resizing.

## Build

On Arch Linux, install the build tools, X server, X11 library, window manager,
and at least one supported terminal emulator:

```sh
sudo pacman -S --needed base-devel libx11 libxrandr xorg-server xorg-xinit xfce4-terminal
```

Resolution controls require the `libxrandr` development package.
Install a browser and file manager of your choice for the corresponding desktop
actions. `xdg-utils` is optional and provides a generic file-opening fallback.

Build once from a terminal:

```sh
make
```

## Login session

Install the binaries and X session entry so a display manager that reads
`/usr/share/xsessions` (including LightDM's XFCE greeter) offers **Field
Desktop** in its session chooser:

```sh
sudo make install
```

Log out, select **Field Desktop** from the session menu on the login screen, and
sign in. The session entry starts the custom window manager and desktop on the
display manager's X display; it does not start a nested X server. `make
install` supports `PREFIX`, `BINDIR`, `XSESSIONDIR`, and `DESTDIR` overrides.
To remove the session, delete
`/usr/share/xsessions/field-desktop.desktop` and the two installed
executables from `/usr/local/bin`.

## Run

From a TTY, log in and run the executable directly:

```sh
./simple-desktop
```

When launched from a virtual console without an X display, the executable
starts X itself, selects a free display, starts `simple-wm`, and then opens the
desktop. Run it from a TTY for the complete custom desktop session. Starting it
inside an existing graphical session uses that session's window manager; the
custom WM cannot take over a display already managed by another WM.

The desktop includes quick actions for the application launcher, terminal, file
manager, browser, and terminal settings. The launcher searches installed
applications and supports keyboard navigation. It reads visible application
entries from `~/.local/share/applications`, `$XDG_DATA_HOME/applications`, and
the `applications` directories under `$XDG_DATA_DIRS` (or
`/usr/local/share:/usr/share` by default). Application `Exec` commands are
started directly, without a shell. File-manager and browser actions prefer
matching installed application entries and fall back to common executable names
when possible. The project launches existing applications; it does not include
its own browser or file manager.

Field Desktop also provides an XEmbed system tray for notification-area
applications. Tray icons appear on the top panel when apps request docking.
The top-right status area always shows system RAM in use / total. Open managed
windows also appear as clickable buttons in the bottom taskbar; selecting one
brings it to the front. If there are more windows than fit, click the `+N more`
area to open the window switcher.

The shell also displays items from `~/Desktop` and refreshes the view when that
folder changes. Double-click a folder or regular file to open it with the
default application (`xdg-open`); `.desktop` launchers use Field Desktop's
desktop-entry parser. Arbitrary executable files are not run directly.

Settings is split into **General**, **Appearance**, and **More** pages. General
selects a terminal, toggles **Performance stats in tray**, and selects a
supported screen resolution. Use Left/Right to select a mode, Enter to test it,
or click a listed mode; confirm within 15 seconds. Escape, Revert, or the
timeout restores the previous size. PageUp/PageDown scrolls the mode list. If
RandR is unavailable, resolution choices are disabled. Screen-size changes
reposition the desktop and tray and keep managed windows reachable.

Appearance offers four high-contrast built-in color themes, solid background choices, and
optional image wallpapers discovered in `~/Pictures` and `~/Desktop`. Built-in
appearance options require no external programs. Image wallpapers are off by
default and use `feh`; Field Desktop only offers its graphical package install
action when explicitly clicked, and that action uses `pkexec` with the Arch
Linux package manager. Selecting an image does not activate it until image
wallpaper is enabled; if it is already enabled, selecting a new image applies
that image immediately. After installing `feh`, click **Apply selected
wallpaper** to activate a previously enabled image. The desktop resets
`SIGCHLD` to its default disposition for launched programs and reaps child
processes itself, so package authorization tools inherit normal child-process
behavior.

The **More** page includes optional XDG user startup applications and lightweight
launcher animations. Both are disabled by default. Startup entries are only
launched on a later login after the setting is enabled. The launcher animation
setting is persisted in `~/.config/simple-desktop/animations`.

Press `p` in Settings to toggle performance stats. When enabled, the status area
also shows CPU percentage and resident memory for Field Desktop and its window
manager, refreshed about once per second. RAM usage is shown as used / total
system memory. The preference is saved in
`~/.config/simple-desktop/performance`. Only one XEmbed tray manager can own a
display; if another desktop environment's tray is already running, its icons
remain with that manager.

The feature code is organized in separate `desktop_items.c`, `appearance.c`,
`startup_apps.c`, and `animations.c` modules. They are built into the desktop
binary; optional startup apps, animations, and image wallpaper remain inactive
unless enabled from Settings.

Use Alt+Space to open the launcher anywhere in the custom session, or
Ctrl+Space when the desktop itself has focus. Type to filter, use Up/Down to
select, and press Enter to launch. Escape closes the launcher. Click Settings to
choose which supported terminal to use; the selection is saved in
`~/.config/simple-desktop/terminal`.

Click the Help desktop icon or press `h` in Settings for the in-session control
guide; Escape or a click closes it.

The handmade WM supports Alt+Tab to cycle windows and show the open-window
switcher, Alt+Shift+Tab to cycle backward, and Alt+Escape to dismiss the
switcher. Click a listed window to focus it. Alt+F4 closes the focused window.
Drag a title bar to move it. Resize a window by dragging its bottom or right
edge, or the bottom-right corner to resize in both directions. Alt+left-drag /
Alt+right-drag also move / resize a window. Windows enforce a minimum size. The
close control is at the right of each title bar. Escape exits the desktop
session.

The optional `run-tty3.sh` script simply invokes the executable from any
working directory.