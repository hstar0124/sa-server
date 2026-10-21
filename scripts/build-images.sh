#!/usr/bin/env bash
# Builds the six container images from deploy/docker/ and tags them sa/<name>:<tag>.
#
#   scripts/build-images.sh [tag]      (default tag: dev)
#
# The C++ build reuses the host's vcpkg binary cache as a seed, so the first image build does not
# compile every dependency again.
set -euo pipefail
cd "$(dirname "$0")/.."

TAG="${1:-dev}"
SEED="${VCPKG_DEFAULT_BINARY_CACHE:-$HOME/.cache/vcpkg/archives}"
mkdir -p "$SEED"

for target in zone gateway loadgen; do
  docker buildx build --load --file deploy/docker/cpp.Dockerfile --target "$target" \
    --build-context vcpkg-seed="$SEED" --tag "sa/$target:$TAG" .
done
for target in backend eventworker migrator; do
  docker buildx build --load --file deploy/docker/dotnet.Dockerfile --target "$target" --tag "sa/$target:$TAG" .
done
docker image ls --filter "reference=sa/*:$TAG"
