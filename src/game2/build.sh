#!/usr/bin/env bash
# Builds Kuru2Client and Kuru2Server into src/game2/build. Works from any cwd.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cmake -S "$here/../.." -B "$here/build"
cmake --build "$here/build" --target Kuru2Client Kuru2Server
