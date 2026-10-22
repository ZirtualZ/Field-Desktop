#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HOST_DISPLAY=${DISPLAY:?Set DISPLAY to the host X server for Xephyr}
DISPLAY_NUMBER=${DISPLAY_NUMBER:-119}
TEST_DISPLAY=":$DISPLAY_NUMBER"
SCREEN_GEOMETRY=${SCREEN_GEOMETRY:-1280x800}
SESSION="$ROOT/simple-desktop"
GOLDEN_DIR="$ROOT/tests/golden/phase0"
TEMP_HOME=
TEMP_OUTPUT=
OUT_DIR=
DESKTOP_PID=
WM_PID=
XEPHYR_PID=
XTERM_PID=

usage()
{
    printf 'Usage: %s --record [output-dir] | --verify\n' "$0" >&2
    exit 2
}

if [ "$#" -eq 0 ]; then
    usage
elif [ "$1" = "--record" ]; then
    if [ "$#" -gt 2 ]; then
        usage
    fi
    OUT_DIR=${2:-$GOLDEN_DIR}
elif [ "$1" = "--verify" ] && [ "$#" -eq 1 ]; then
    OUT_DIR=
    TEMP_OUTPUT=$(mktemp -d)
    OUT_DIR=$TEMP_OUTPUT
else
    usage
fi

for tool in Xephyr xdotool xdpyinfo import xwininfo xterm ps awk; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'Missing characterization tool: %s\n' "$tool" >&2
        exit 1
    }
done
for tool in grep head pgrep getconf wc mktemp; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf 'Missing characterization tool: %s\n' "$tool" >&2
        exit 1
    }
done
if [ "$1" = "--verify" ]; then
    command -v compare >/dev/null 2>&1 || {
        printf 'Missing image comparison tool: compare (ImageMagick)\n' >&2
        exit 1
    }
fi

mkdir -p "$OUT_DIR"
TEMP_HOME=$(mktemp -d)
mkdir -p "$TEMP_HOME/Desktop" "$TEMP_HOME/.config/simple-desktop"
printf 'memory_status=0\n' > "$TEMP_HOME/.config/simple-desktop/features"

cleanup()
{
    if [ -n "$XTERM_PID" ] && kill -0 "$XTERM_PID" 2>/dev/null; then
        kill "$XTERM_PID" 2>/dev/null || true
    fi
    if [ -n "$DESKTOP_PID" ] && kill -0 "$DESKTOP_PID" 2>/dev/null; then
        kill "$DESKTOP_PID" 2>/dev/null || true
    fi
    if [ -n "$WM_PID" ] && kill -0 "$WM_PID" 2>/dev/null; then
        kill "$WM_PID" 2>/dev/null || true
    fi
    if [ -n "$XEPHYR_PID" ] && kill -0 "$XEPHYR_PID" 2>/dev/null; then
        kill "$XEPHYR_PID" 2>/dev/null || true
    fi
    if [ -n "$DESKTOP_PID" ]; then
        wait "$DESKTOP_PID" 2>/dev/null || true
    fi
    if [ -n "$WM_PID" ]; then
        wait "$WM_PID" 2>/dev/null || true
    fi
    if [ -n "$XEPHYR_PID" ]; then
        wait "$XEPHYR_PID" 2>/dev/null || true
    fi
    if [ -n "$TEMP_HOME" ]; then
        rm -rf -- "$TEMP_HOME"
    fi
    if [ -n "$TEMP_OUTPUT" ]; then
        rm -rf -- "$TEMP_OUTPUT"
    fi
}
trap cleanup EXIT HUP INT TERM

DISPLAY="$HOST_DISPLAY" Xephyr "$TEST_DISPLAY" \
    -screen "$SCREEN_GEOMETRY" -ac -noreset -nolisten tcp \
    > "$TEMP_HOME/xephyr.log" 2>&1 &
XEPHYR_PID=$!

ready=0
attempt=0
while [ "$attempt" -lt 50 ]; do
    if DISPLAY="$TEST_DISPLAY" xdpyinfo >/dev/null 2>&1; then
        ready=1
        break
    fi
    if ! kill -0 "$XEPHYR_PID" 2>/dev/null; then
        break
    fi
    attempt=$((attempt + 1))
    sleep 0.1
done
if [ "$ready" -ne 1 ]; then
    printf 'Xephyr did not become ready on %s\n' "$TEST_DISPLAY" >&2
    exit 1
fi

DISPLAY="$TEST_DISPLAY" HOME="$TEMP_HOME" \
XDG_CONFIG_HOME="$TEMP_HOME/.config" \
XDG_DATA_HOME="$TEMP_HOME/.local/share" \
"$SESSION" --x-session > "$TEMP_HOME/session.log" 2>&1 &
DESKTOP_PID=$!

wait_for_window()
{
    name=$1
    attempt=0
    while [ "$attempt" -lt 50 ]; do
        if DISPLAY="$TEST_DISPLAY" xdotool search --onlyvisible \
            --name "$name" >/dev/null 2>&1; then
            return 0
        fi
        if ! kill -0 "$DESKTOP_PID" 2>/dev/null; then
            printf 'Desktop session exited before window appeared: %s\n' "$name" >&2
            return 1
        fi
        attempt=$((attempt + 1))
        sleep 0.1
    done
    printf 'Timed out waiting for window: %s\n' "$name" >&2
    return 1
}

attempt=0
while [ "$attempt" -lt 50 ]; do
    WM_PID=$(pgrep -P "$DESKTOP_PID" -x simple-wm 2>/dev/null | head -n 1 || true)
    if [ -n "$WM_PID" ] &&
       DISPLAY="$TEST_DISPLAY" xwininfo -root -tree 2>/dev/null |
           grep -q '1280x800+0+0'; then
        break
    fi
    if ! kill -0 "$DESKTOP_PID" 2>/dev/null; then
        printf 'Desktop session exited during startup.\n' >&2
        exit 1
    fi
    attempt=$((attempt + 1))
    sleep 0.1
done
if [ -z "$WM_PID" ]; then
    printf 'Window manager did not start.\n' >&2
    exit 1
fi
sleep 2

capture()
{
    DISPLAY="$TEST_DISPLAY" import -window root "$OUT_DIR/$1.png"
}

capture desktop

CLK_TCK=$(getconf CLK_TCK)
process_ticks()
{
    awk '{ print $14 + $15 }' "/proc/$1/stat"
}
system_ticks()
{
    awk '/^cpu / { total = 0; for (i = 2; i <= NF; ++i) total += $i; print total; exit }' /proc/stat
}
record_metrics()
{
    process=$1
    label=$2
    rss=$(ps -o rss= -p "$process" | awk '{print $1}')
    before_process=$(process_ticks "$process")
    before_system=$(system_ticks)
    sleep 5
    after_process=$(process_ticks "$process")
    after_system=$(system_ticks)
    cpu=$(awk -v p="$after_process" -v p0="$before_process" \
        -v clock="$CLK_TCK" 'BEGIN { printf "%.2f", (p-p0)*100/clock/5 }')
    printf '%s pid=%s idle_rss_kb=%s idle_cpu_percent=%s\n' \
        "$label" "$process" "$rss" "$cpu" >> "$OUT_DIR/metrics.txt"
}

{
    printf 'display=%s\ngeometry=%s\n' "$TEST_DISPLAY" "$SCREEN_GEOMETRY"
    printf 'simple-desktop_bytes=%s\n' "$(wc -c < "$SESSION")"
    printf 'simple-wm_bytes=%s\n' "$(wc -c < "$ROOT/simple-wm")"
} > "$OUT_DIR/metrics.txt"
record_metrics "$DESKTOP_PID" simple-desktop
record_metrics "$WM_PID" simple-wm

DISPLAY="$TEST_DISPLAY" xdotool key --clearmodifiers alt+space
sleep 1
attempt=0
while ! DISPLAY="$TEST_DISPLAY" xwininfo -root -tree 2>/dev/null |
    grep -q '520x430+'; do
    if [ "$attempt" -ge 50 ]; then
        printf 'Timed out waiting for the launcher window.\n' >&2
        exit 1
    fi
    attempt=$((attempt + 1))
    sleep 0.1
done
capture launcher
DISPLAY="$TEST_DISPLAY" xdotool key Escape
sleep 0.5

DISPLAY="$TEST_DISPLAY" xdotool mousemove 455 125 click 1
wait_for_window 'Field Desktop Settings'
sleep 1
capture settings-general
for page in 2 3 4; do
    DISPLAY="$TEST_DISPLAY" xdotool key "$page"
    sleep 0.5
    capture "settings-page-$page"
done
DISPLAY="$TEST_DISPLAY" xdotool key Escape
sleep 0.5

DISPLAY="$TEST_DISPLAY" xterm -T "Baseline Test Window" \
    -geometry 48x12+80+90 -e /bin/sh -c \
    'printf "Field Desktop baseline test window\\n"; exec sleep 120' \
    > "$TEMP_HOME/xterm.log" 2>&1 &
XTERM_PID=$!
wait_for_window 'Baseline Test Window'
sleep 1
capture test-window
DISPLAY="$TEST_DISPLAY" xdotool key --clearmodifiers alt+F4
attempt=0
while DISPLAY="$TEST_DISPLAY" xdotool search --onlyvisible \
    --name 'Baseline Test Window' >/dev/null 2>&1; do
    if [ "$attempt" -ge 50 ]; then
        printf 'Timed out waiting for test window to close.\n' >&2
        exit 1
    fi
    attempt=$((attempt + 1))
    sleep 0.1
done
wait "$XTERM_PID" 2>/dev/null || true
XTERM_PID=
sleep 0.5
capture test-window-closed

if [ "$1" = "--verify" ]; then
    for image in desktop launcher settings-general settings-page-2 \
        settings-page-3 settings-page-4 test-window test-window-closed; do
        compare -metric AE "$GOLDEN_DIR/$image.png" \
            "$OUT_DIR/$image.png" null: 2>/dev/null || {
            printf 'Golden image differs: %s.png\n' "$image" >&2
            exit 1
        }
    done
    printf 'Characterization screenshots match Phase 0 goldens.\n'
else
    printf 'Characterization screenshots and metrics recorded in %s\n' "$OUT_DIR"
fi
