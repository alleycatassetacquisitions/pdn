#!/bin/bash
# Regenerate compile_commands.json from native_cli outside a pio run; touch forces
# clangd's watcher to reload; flock makes concurrent runs exit.

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
