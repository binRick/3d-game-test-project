#!/bin/bash
set -e
cd "$(dirname "$0")"
pkill -x IronFist3D 2>/dev/null || true
make -B -s
open IronFist3D.app
