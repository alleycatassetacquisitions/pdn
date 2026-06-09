#!/bin/bash
# Refresh clangd's view of the project. Run this whenever code state changes
# outside of a `pio run`/`pio test` invocation (e.g. after an Agent completes
# or after direct file edits). PlatformIO's own auto_compiledb.py covers pio
# invocations; this script covers the gap.
#
# What it does:
#   1. Regenerates compile_commands.json from native_cli env (canonical).
#   2. Touches the file to force mtime bump, so clangd's inotify watcher
#      definitely picks up a change even when content is identical.
#
# A lockfile prevents concurrent runs from racing each other. If another
# instance is already refreshing, this one exits immediately.

set -u
PROJECT_DIR="/home/tirefire/pdn"
LOCKFILE="$PROJECT_DIR/.pio/.compiledb_refresh.lock"

mkdir -p "$(dirname "$LOCKFILE")"
exec 9>"$LOCKFILE"
if ! flock -n 9; then
    exit 0
fi

cd "$PROJECT_DIR" || exit 0
pio run -e native_cli -t compiledb -s >/dev/null 2>&1
touch "$PROJECT_DIR/compile_commands.json"
