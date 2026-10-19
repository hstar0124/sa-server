#pragma once

#include "sa/common/config_source.hpp"
#include "sa/common/framing.hpp"
#include "sa/common/ids.hpp"
#include "sa/common/logging.hpp"
#include "sa/zone/sim/duration.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

namespace sa::zone {

// Everything the zone process can be tuned with, in one place. Values come from the TOML file
// given on the command line plus SA_ZONE__* environment overrides.
struct ZoneServerConfig {
    common::WorldId world_id;
    common::ZoneId zone_id;
    sim::TickRate simulation_rate{};
    sim::TickRate snapshot_rate{};  // must divide simulation_rate
    std::uint32_t max_catch_up_ticks = 0;
    std::uint64_t rng_seed = 0;  // 0 selects a seed from the OS and logs it
    std::chrono::seconds stats_log_interval{};
    common::LoggingConfig logging;

    std::filesystem::path map_file;
    std::filesystem::path movement_rules_file;
    std::filesystem::path content_dir;  // game-data root: version, classes, skills, monsters

    // Gateway-facing link (contracts/internal).
    std::string link_listen_address;
    std::uint16_t link_listen_port = 0;
    std::uint32_t link_max_gateways = 0;
    common::FrameLimits link_frame_limits;
    std::uint32_t max_sessions = 0;

    // Backend gRPC and lease cadence (see README "소유권과 lease").
    std::string backend_address;
    std::chrono::milliseconds backend_rpc_timeout{};
    std::chrono::milliseconds lease_renew_interval{};
    std::chrono::milliseconds lease_safety_margin{};

    // How long shutdown waits for evictions, detaches and lease RPCs to drain (design
    // document 16.3).
    std::chrono::milliseconds shutdown_grace{};

    // Development only: bots driven in-process without a network (ADR-0006). 0 in deployment.
    std::uint32_t local_bots = 0;

    [[nodiscard]] std::uint32_t TicksPerSnapshot() const noexcept {
        return simulation_rate.hz / snapshot_rate.hz;
    }

    static constexpr std::string_view kEnvPrefix = "SA_ZONE__";

    [[nodiscard]] static std::expected<ZoneServerConfig, common::ConfigError> FromConfig(
        const common::ConfigSource& source);
};

}  // namespace sa::zone
