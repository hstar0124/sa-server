#!/usr/bin/env bash
# Formats every C++ and C# source in place. CI runs the same tools in --check mode.
set -euo pipefail
cd "$(dirname "$0")/.."

find server/cpp tools -type f \( -name '*.hpp' -o -name '*.cpp' \) -print0 \
  | xargs -0 clang-format -i

dotnet format server/dotnet/GameServer.slnx --verbosity quiet
