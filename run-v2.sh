#!/bin/bash
set -e
cd "$(dirname "$0")"

if [[ "$(uname)" == "Darwin" ]]; then
    need=()
    [[ -f /opt/homebrew/opt/raylib/lib/libraylib.dylib || \
       -f /usr/local/opt/raylib/lib/libraylib.dylib ]] || need+=(raylib)
    command -v pkg-config >/dev/null 2>&1 || need+=(pkg-config)
    if (( ${#need[@]} > 0 )); then
        if ! command -v brew >/dev/null 2>&1; then
            echo "Missing deps (${need[*]}) and Homebrew not found. Install from https://brew.sh and re-run." >&2
            exit 1
        fi
        echo "Installing missing dependencies: ${need[*]}"
        brew install "${need[@]}"
    fi
fi

pkill -x IronFist3D-v2 2>/dev/null || true
make v2 -B -s
exec IronFist3D-v2.app/Contents/MacOS/IronFist3D-v2 --debug
