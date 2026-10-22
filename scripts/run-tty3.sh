#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DESKTOP="$PROJECT_DIR/simple-desktop"

if [ ! -x "$DESKTOP" ]; then
    printf 'Desktop executable not found. Build it first with: make -C "%s"\n' "$PROJECT_DIR" >&2
    exit 1
fi

exec "$DESKTOP"