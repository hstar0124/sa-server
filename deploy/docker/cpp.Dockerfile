# syntax=docker/dockerfile:1.7
# C++ servers and the bot client. One build stage, three runtime images picked with --target
# (zone, gateway, loadgen). The builder mirrors the development machine: Rocky Linux 9 with
# gcc-toolset-14 and vcpkg at the manifest baseline, so archives in the vcpkg binary cache are
# reusable between the two (scripts/build-images.sh seeds the cache from the host).
FROM rockylinux:9 AS builder
# ninja lives in the CodeReady Builder repository on Rocky 9.
RUN dnf -y install dnf-plugins-core && dnf config-manager --set-enabled crb \
    && dnf -y install gcc-toolset-14 cmake ninja-build git zip unzip tar perl python3 \
        pkgconf-pkg-config autoconf automake libtool bison flex \
    && dnf clean all
ENV VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DEFAULT_BINARY_CACHE=/root/.cache/vcpkg/archives
WORKDIR /src
COPY vcpkg.json ./
RUN BASELINE=$(python3 -c "import json;print(json.load(open('vcpkg.json'))['builtin-baseline'])") \
    && git clone -q https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT" \
    && git -C "$VCPKG_ROOT" checkout -q "$BASELINE" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
COPY . .
# The cache mount keeps compiled dependencies between image builds; the seed context carries the
# host's archives in on the first build (an empty directory when there are none).
RUN --mount=type=cache,target=/root/.cache/vcpkg/archives \
    --mount=type=bind,from=vcpkg-seed,target=/seed \
    { cp -rn /seed/. "$VCPKG_DEFAULT_BINARY_CACHE"/ 2>/dev/null || true; } \
    && scl enable gcc-toolset-14 -- cmake --preset release -DSA_BUILD_TESTS=OFF \
    && scl enable gcc-toolset-14 -- cmake --build --preset release --target zone_server gateway loadgen

# Runtime: the minimal Rocky image plus the C++ runtime. gcc-toolset links the newer parts of the
# standard library statically, so the system libstdc++ suffices.
FROM rockylinux:9-minimal AS runtime
RUN microdnf -y install libstdc++ openssl-libs ca-certificates && microdnf clean all
WORKDIR /app
COPY --from=builder /src/game-data ./game-data
# Non-root with a fixed uid, so a manifest can name it.
USER 10001

FROM runtime AS zone
COPY --from=builder /src/build/release/server/cpp/zone/server/zone_server /usr/local/bin/zone_server
ENTRYPOINT ["zone_server"]
CMD ["--config", "/etc/sa/zone.toml"]

FROM runtime AS gateway
COPY --from=builder /src/build/release/server/cpp/gateway/gateway /usr/local/bin/gateway
ENTRYPOINT ["gateway"]
CMD ["--config", "/etc/sa/gateway.toml"]

FROM runtime AS loadgen
COPY --from=builder /src/build/release/tools/loadgen/client/loadgen /usr/local/bin/loadgen
COPY --from=builder /src/tools/loadgen/client/profiles ./profiles
ENTRYPOINT ["loadgen"]
CMD ["--profile", "/app/profiles/smoke.toml"]
