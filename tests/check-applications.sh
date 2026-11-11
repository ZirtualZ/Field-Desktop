#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TEST_DIR=$(mktemp -d)
trap 'rm -rf -- "$TEST_DIR"' EXIT HUP INT TERM

"${CC:-cc}" ${CFLAGS:--O2 -Wall -Wextra -Wpedantic} \
    -I"$ROOT/src" -std=c11 \
    "$ROOT/tests/applications_test.c" "$ROOT/src/applications.c" \
    -o "$TEST_DIR/applications-test"
"$TEST_DIR/applications-test" "$ROOT/tests/fixtures/terminal.desktop"
printf 'Application metadata checks passed.\n'
