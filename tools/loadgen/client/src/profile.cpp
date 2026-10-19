#include "profile.hpp"

#include <algorithm>
#include <optional>

namespace sa::loadgen {
std::expected<LoadProfile, common::ConfigError> LoadProfile::FromConfig(
    const common::ConfigSource& source) {
    const auto backend = source.Require<std::string>("backend.base_url");
    const auto world = source.Require<std::uint64_t>("world.id");
    const auto class_id = source.Require<std::uint32_t>("world.class_id");
    const auto wanderers = source.Require<std::uint32_t>("bots.wanderers");
    const auto fighters = source.Require<std::uint32_t>("bots.fighters");
    const auto seed = source.Require<std::uint64_t>("bots.seed");
    const auto prefix = source.Require<std::string>("bots.account_prefix");
    const auto password = source.Require<std::string>("bots.password");
    const auto duration = source.Require<std::uint32_t>("run.duration_sec");
    const auto min_seeing = source.Require<std::uint32_t>("run.min_bots_seeing_others");
    const auto min_kills = source.Require<std::uint32_t>("run.min_kills");
    const auto tick_hz = source.Require<std::uint32_t>("client.tick_hz");
    const auto ping = source.Require<std::uint32_t>("client.ping_interval_ms");
    const auto connect = source.Require<std::uint32_t>("client.connect_timeout_ms");
    const auto arrive = source.Require<double>("wanderer.arrive_distance_m");
    const auto retarget = source.Require<std::uint32_t>("wanderer.retarget_after_ticks");
    const auto idle_min = source.Require<std::uint32_t>("wanderer.idle_ticks_min");
    const auto idle_max = source.Require<std::uint32_t>("wanderer.idle_ticks_max");
    const auto margin = source.Require<double>("wanderer.margin_m");
    const auto engage = source.Require<double>("fighter.engage_radius_m");
    const auto attack_skill = source.Require<std::uint32_t>("fighter.attack_skill_id");
    const auto attack_range = source.Require<double>("fighter.attack_range_m");
    const auto attack_interval = source.Require<std::uint32_t>("fighter.attack_interval_ms");
    if (auto err = common::FirstConfigError(
            backend, world, class_id, wanderers, fighters, seed, prefix, password, duration,
            tick_hz, ping, connect, arrive, retarget, idle_min, idle_max, margin, min_seeing,
            min_kills, engage, attack_skill, attack_range, attack_interval)) {
        return std::unexpected(*err);
    }
    if (*wanderers + *fighters == 0 || *tick_hz == 0 || *duration == 0) {
        return std::unexpected(common::ConfigError{
            "at least one bot, client.tick_hz and run.duration_sec must be positive"});
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
    out.wanderers = *wanderers;
    out.fighters = *fighters;
    out.seed = *seed;
    out.account_prefix = *prefix;
    out.password = *password;
    out.duration = std::chrono::seconds{*duration};
    out.min_bots_seeing_others = *min_seeing;
    out.min_kills = *min_kills;
    out.client_tick_hz = *tick_hz;
    out.ping_interval = std::chrono::milliseconds{*ping};
    out.connect_timeout = std::chrono::milliseconds{*connect};
    out.wanderer = bot::WandererParams{.arrive_distance_m = static_cast<float>(*arrive),
                                       .retarget_after_ticks = *retarget,
                                       .idle_ticks_min = *idle_min,
                                       .idle_ticks_max = *idle_max,
                                       .margin_m = static_cast<float>(*margin)};
    // Interval in ticks of the client clock; the zone enforces the skill's real cooldown.
    const std::uint32_t interval_ticks = (*attack_interval * *tick_hz) / 1000;
    out.fighter = bot::FighterParams{.engage_radius_m = static_cast<float>(*engage),
                                     .attack_skill_id = *attack_skill,
                                     .attack_range_m = static_cast<float>(*attack_range),
                                     .attack_interval_ticks = std::max(1U, interval_ticks),
                                     .roam = out.wanderer};
    return out;
}

}  // namespace sa::loadgen
