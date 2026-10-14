#!/usr/bin/env bash
# Fails if any source is not formatted. Used by CI.
set -euo pipefail
cd "$(dirname "$0")/.."

find server/cpp tools -type f \( -name '*.hpp' -o -name '*.cpp' \) -print0 \
  | xargs -0 clang-format --dry-run --Werror

dotnet format server/dotnet/GameServer.slnx --verify-no-changes --verbosity quiet
