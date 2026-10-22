#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DESKTOP="$SCRIPT_DIR/simple-desktop"

if [ ! -x "$DESKTOP" ]; then
    printf 'Desktop executable not found. Build it first with: make -C "%s"\n' "$SCRIPT_DIR" >&2
    exit 1
fi

exec "$DESKTOP"