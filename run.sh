#!/bin/bash
set -e
cd "$(dirname "$0")"

# Dep check — fast filesystem / PATH lookups on the happy path. Each `brew`
# invocation costs hundreds of ms (it resolves config + taps), so we only
# shell out to it when something is actually missing, then install the whole
# set in one call.
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

pkill -x IronFist3D 2>/dev/null || true
make -B -s
# Run the binary directly (not via `open`) so it stays attached to this
# terminal — Ctrl+C kills it and stdout/stderr land here. `open` would
# detach as a separate Dock app and leave nothing to interrupt.
# --debug enables /tmp/ironfist-debug.log (5 Hz player/enemy state snapshot).
# Tail it via ./debug.sh.
exec IronFist3D.app/Contents/MacOS/IronFist3D --debug
