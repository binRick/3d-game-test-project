#!/bin/bash
set -e
cd "$(dirname "$0")"
make -B -s
open IronFist3D.app
