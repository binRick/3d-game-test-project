#!/bin/bash
set -e
cd "$(dirname "$0")"

# Install macOS deps via Homebrew if missing. Requires brew.
if [[ "$(uname)" == "Darwin" ]]; then
    if ! command -v brew >/dev/null 2>&1; then
        echo "Homebrew not found. Install from https://brew.sh and re-run." >&2
        exit 1
    fi
    for pkg in raylib pkg-config; do
        if ! brew list --formula "$pkg" >/dev/null 2>&1; then
            echo "Installing missing dependency: $pkg"
            brew install "$pkg"
        fi
    done
fi

pkill -x IronFist3D 2>/dev/null || true
make -B -s
open IronFist3D.app
