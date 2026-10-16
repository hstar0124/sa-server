#!/usr/bin/env bash
# Serves the repository root so the viewer can fetch the .proto contracts and game-data it draws.
set -euo pipefail
cd "$(dirname "$0")/.."
PORT="${1:-8080}"
echo "viewer: http://localhost:$PORT/tools/viewer/"
exec python3 -m http.server "$PORT" --bind 127.0.0.1
