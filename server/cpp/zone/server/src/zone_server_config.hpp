#pragma once

#include "sa/bot/wanderer.hpp"
#include "sa/common/config_source.hpp"
#include "sa/common/framing.hpp"
#include "sa/common/ids.hpp"
#include "sa/common/logging.hpp"
#include "sa/zone/sim/duration.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace sa::zone {

// Everything the zone process can be tuned with, in one place. Values come from the TOML file
// given on the command line plus SA_ZONE__* environment overrides.
// In-process bots for simulation development ([dev]); the class and wanderer values are read only
// when `count` is positive.
struct DevBotConfig {
    std::uint32_t count = 0;
    common::ClassId class_id;
    bot::WandererParams wanderer;
};

struct ZoneServerConfig {
    common::WorldId world_id;
    common::ZoneId zone_id;
    sim::TickRate simulation_rate{};
    sim::TickRate snapshot_rate{};  // must divide simulation_rate
    std::uint32_t max_catch_up_ticks = 0;
    std::optional<std::uint64_t> rng_seed;  // absent: a seed from the OS, logged
    std::chrono::seconds stats_log_interval{};
    common::LoggingConfig logging;

    std::filesystem::path map_file;
    std::filesystem::path movement_rules_file;
    std::filesystem::path economy_rules_file;
    std::filesystem::path
        content_dir;  // game-data root: version, classes, skills, monsters, items, drops

    // Gateway-facing link (contracts/internal).
    std::string link_listen_address;
    std::uint16_t link_listen_port = 0;
    std::uint32_t link_max_gateways = 0;
    common::FrameLimits link_frame_limits;
    std::chrono::milliseconds link_close_linger{};
    // A gateway link that carries nothing this long is closed; gateways ping well within it.
    std::chrono::milliseconds link_silence_timeout{};
    std::uint32_t max_sessions = 0;

    // Backend gRPC and lease cadence (see README "소유권과 lease").
    std::string backend_address;
    std::chrono::milliseconds backend_rpc_timeout{};
    std::chrono::milliseconds lease_renew_interval{};
    std::chrono::milliseconds lease_safety_margin{};
    // Tries per Backend call on transport failures; every call is safe to repeat (see
    // backend_call.hpp).
    std::uint32_t backend_call_attempts = 0;
    std::chrono::milliseconds backend_retry_delay{};
    // How often each session's position is saved while it moves (ADR-0010).
    std::chrono::milliseconds checkpoint_interval{};

    // How long shutdown waits for evictions, detaches and lease RPCs to drain (design
    // document 16.3).
    std::chrono::milliseconds shutdown_grace{};

    // Development only: bots driven in-process without a network (ADR-0006). 0 in deployment.
    DevBotConfig dev_bots;

    [[nodiscard]] std::uint32_t TicksPerSnapshot() const noexcept {
        return simulation_rate.hz / snapshot_rate.hz;
    }
    // At least one: validation refuses an interval shorter than a tick.
    [[nodiscard]] std::uint32_t TicksPerCheckpoint() const noexcept {
        return static_cast<std::uint32_t>(
            std::chrono::duration_cast<sim::Duration>(checkpoint_interval) /
            simulation_rate.Period());
    }

    static constexpr std::string_view kEnvPrefix = "SA_ZONE__";

    [[nodiscard]] static std::expected<ZoneServerConfig, common::ConfigError> FromConfig(
        const common::ConfigSource& source);
};

}  // namespace sa::zone
