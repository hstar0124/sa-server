#include "profile.hpp"

#include <optional>

namespace sa::loadgen {
std::expected<LoadProfile, common::ConfigError> LoadProfile::FromConfig(
    const common::ConfigSource& source) {
    const auto backend = source.Require<std::string>("backend.base_url");
    const auto world = source.Require<std::uint64_t>("world.id");
    const auto class_id = source.Require<std::uint32_t>("world.class_id");
    const auto count = source.Require<std::uint32_t>("bots.count");
    const auto seed = source.Require<std::uint64_t>("bots.seed");
    const auto prefix = source.Require<std::string>("bots.account_prefix");
    const auto password = source.Require<std::string>("bots.password");
    const auto duration = source.Require<std::uint32_t>("run.duration_sec");
    const auto min_seeing = source.Require<std::uint32_t>("run.min_bots_seeing_others");
    const auto tick_hz = source.Require<std::uint32_t>("client.tick_hz");
    const auto ping = source.Require<std::uint32_t>("client.ping_interval_ms");
    const auto connect = source.Require<std::uint32_t>("client.connect_timeout_ms");
    const auto arrive = source.Require<double>("wanderer.arrive_distance_m");
    const auto retarget = source.Require<std::uint32_t>("wanderer.retarget_after_ticks");
    const auto idle_min = source.Require<std::uint32_t>("wanderer.idle_ticks_min");
    const auto idle_max = source.Require<std::uint32_t>("wanderer.idle_ticks_max");
    const auto margin = source.Require<double>("wanderer.margin_m");
    if (auto err = common::FirstConfigError(backend, world, class_id, count, seed, prefix, password,
                                            duration, tick_hz, ping, connect, arrive, retarget,
                                            idle_min, idle_max, margin, min_seeing)) {
        return std::unexpected(*err);
    }
    if (*count == 0 || *tick_hz == 0 || *duration == 0) {
        return std::unexpected(common::ConfigError{
            "bots.count, client.tick_hz and run.duration_sec must be positive"});
    }
    if (*idle_min > *idle_max) {
        return std::unexpected(
            common::ConfigError{"wanderer.idle_ticks_min must not exceed idle_ticks_max"});
    }

    LoadProfile out;
    out.backend_base_url = *backend;
    out.gateway_host = source.GetOr<std::string>("gateway.host", "");
    out.gateway_port = source.GetOr<std::uint16_t>("gateway.port", 0);
    out.world_id = *world;
    out.class_id = *class_id;
    out.bot_count = *count;
    out.seed = *seed;
    out.account_prefix = *prefix;
    out.password = *password;
    out.duration = std::chrono::seconds{*duration};
    out.min_bots_seeing_others = *min_seeing;
    out.client_tick_hz = *tick_hz;
    out.ping_interval = std::chrono::milliseconds{*ping};
    out.connect_timeout = std::chrono::milliseconds{*connect};
    out.wanderer = bot::WandererParams{.arrive_distance_m = static_cast<float>(*arrive),
                                       .retarget_after_ticks = *retarget,
                                       .idle_ticks_min = *idle_min,
                                       .idle_ticks_max = *idle_max,
                                       .margin_m = static_cast<float>(*margin)};
    return out;
}

}  // namespace sa::loadgen
