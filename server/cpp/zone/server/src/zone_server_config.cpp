#include "zone_server_config.hpp"

namespace sa::zone {

std::expected<ZoneServerConfig, common::ConfigError> ZoneServerConfig::FromConfig(
    const common::ConfigSource& source) {
    const auto world_id = source.Require<std::uint64_t>("zone.world_id");
    const auto zone_id = source.Require<std::uint32_t>("zone.zone_id");
    const auto hz = source.Require<std::uint32_t>("zone.simulation_hz");
    const auto snapshot_hz = source.Require<std::uint32_t>("zone.snapshot_hz");
    const auto catch_up = source.Require<std::uint32_t>("zone.max_catch_up_ticks");
    const auto stats_interval = source.Require<std::uint32_t>("zone.stats_log_interval_sec");
    const auto max_sessions = source.Require<std::uint32_t>("zone.max_sessions");
    const auto map_file = source.Require<std::string>("content.map_file");
    const auto rules_file = source.Require<std::string>("content.movement_rules_file");
    const auto economy_file = source.Require<std::string>("content.economy_rules_file");
    const auto content_dir = source.Require<std::string>("content.dir");
    const auto link_address = source.Require<std::string>("gateway_link.listen_address");
    const auto link_port = source.Require<std::uint16_t>("gateway_link.listen_port");
    const auto link_max = source.Require<std::uint32_t>("gateway_link.max_gateways");
    const auto link_frame = source.Require<std::size_t>("gateway_link.max_frame_bytes");
    const auto link_linger = source.Require<std::uint32_t>("gateway_link.close_linger_ms");
    const auto backend_address = source.Require<std::string>("backend.address");
    const auto rpc_timeout = source.Require<std::uint32_t>("backend.rpc_timeout_ms");
    const auto renew_interval = source.Require<std::uint32_t>("backend.lease_renew_interval_ms");
    const auto safety_margin = source.Require<std::uint32_t>("backend.lease_safety_margin_ms");
    const auto call_attempts = source.Require<std::uint32_t>("backend.economy_call_attempts");
    const auto retry_delay = source.Require<std::uint32_t>("backend.retry_delay_ms");
    const auto shutdown_grace = source.Require<std::uint32_t>("shutdown.grace_ms");
    if (auto err = common::FirstConfigError(
            world_id, zone_id, hz, snapshot_hz, catch_up, stats_interval, max_sessions, map_file,
            rules_file, economy_file, content_dir, link_address, link_port, link_max, link_frame,
            link_linger, backend_address, rpc_timeout, renew_interval, safety_margin, call_attempts,
            retry_delay, shutdown_grace)) {
        return std::unexpected(*err);
    }
    // Positive and slow enough that one tick lasts at least one clock unit (the period divides by
    // the rate).
    if (*hz == 0 || sim::TickRate{.hz = *hz}.Period() == sim::Duration::zero()) {
        return std::unexpected(
            common::ConfigError{"zone.simulation_hz must be positive and give a nonzero period"});
    }
    if (*snapshot_hz == 0 || *hz % *snapshot_hz != 0) {
        return std::unexpected(
            common::ConfigError{"zone.snapshot_hz must be positive and divide zone.simulation_hz"});
    }
    if (*renew_interval == 0 || *rpc_timeout == 0 || *call_attempts == 0 || *retry_delay == 0) {
        return std::unexpected(common::ConfigError{
            "backend intervals, retry_delay_ms and economy_call_attempts must be positive"});
    }
    // Zero would refuse every gateway or frame, skip every catch-up tick, or re-arm the stats
    // timer without pause.
    if (*catch_up == 0 || *stats_interval == 0 || *max_sessions == 0 || *link_max == 0 ||
        *link_frame == 0 || *link_linger == 0) {
        return std::unexpected(common::ConfigError{
            "zone.max_catch_up_ticks, zone.stats_log_interval_sec, zone.max_sessions and the "
            "gateway_link limits must be positive"});
    }
    auto logging = common::LoggingConfig::FromConfig(source);
    if (!logging) {
        return std::unexpected(logging.error());
    }

    ZoneServerConfig out;
    out.world_id = common::WorldId{*world_id};
    out.zone_id = common::ZoneId{*zone_id};
    out.simulation_rate = sim::TickRate{.hz = *hz};
    out.snapshot_rate = sim::TickRate{.hz = *snapshot_hz};
    out.max_catch_up_ticks = *catch_up;
    const auto rng_seed = source.Optional<std::uint64_t>("zone.rng_seed");
    const auto local_bots = source.GetOr<std::uint32_t>("dev.local_bots", 0);
    if (auto err = common::FirstConfigError(rng_seed, local_bots)) {
        return std::unexpected(*err);
    }
    out.rng_seed = *rng_seed;
    out.stats_log_interval = std::chrono::seconds{*stats_interval};
    out.max_sessions = *max_sessions;
    out.map_file = *map_file;
    out.movement_rules_file = *rules_file;
    out.economy_rules_file = *economy_file;
    out.content_dir = *content_dir;
    out.local_bots = *local_bots;
    out.link_listen_address = *link_address;
    out.link_listen_port = *link_port;
    out.link_max_gateways = *link_max;
    out.link_frame_limits.max_payload_bytes = *link_frame;
    out.link_close_linger = std::chrono::milliseconds{*link_linger};
    out.backend_address = *backend_address;
    out.backend_rpc_timeout = std::chrono::milliseconds{*rpc_timeout};
    out.lease_renew_interval = std::chrono::milliseconds{*renew_interval};
    out.lease_safety_margin = std::chrono::milliseconds{*safety_margin};
    out.economy_call_attempts = *call_attempts;
    out.backend_retry_delay = std::chrono::milliseconds{*retry_delay};
    out.shutdown_grace = std::chrono::milliseconds{*shutdown_grace};
    out.logging = *logging;
    return out;
}

}  // namespace sa::zone
