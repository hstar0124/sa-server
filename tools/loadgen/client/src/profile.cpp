#include "profile.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <optional>
#include <random>

namespace sa::loadgen {
std::expected<LoadProfile, common::ConfigError> LoadProfile::FromConfig(
    const common::ConfigSource& source) {
    const auto backend = source.Require<std::string>("backend.base_url");
    const auto world = source.Require<std::uint64_t>("world.id");
    const auto class_id = source.Require<std::uint32_t>("world.class_id");
    const auto wanderers = source.Require<std::uint32_t>("bots.wanderers");
    const auto fighters = source.Require<std::uint32_t>("bots.fighters");
    const auto looters = source.Require<std::uint32_t>("bots.looters");
    const auto travelers = source.Require<std::uint32_t>("bots.travelers");
    const auto traders = source.Require<std::uint32_t>("bots.traders");
    const auto seed = source.Require<std::uint64_t>("bots.seed");
    const auto prefix = source.Require<std::string>("bots.account_prefix");
    const auto identity_seed = source.Optional<std::uint64_t>("bots.identity_seed");
    const auto password = source.Require<std::string>("bots.password");
    const auto duration = source.Require<std::uint32_t>("run.duration_sec");
    const auto min_seeing = source.Require<std::uint32_t>("run.min_bots_seeing_others");
    const auto min_kills = source.Require<std::uint32_t>("run.min_kills");
    const auto min_pickups = source.Require<std::uint32_t>("run.min_pickups");
    const auto min_zone_changes = source.Require<std::uint32_t>("run.min_zone_changes");
    const auto min_settlements = source.Require<std::uint32_t>("run.min_trade_settlements");
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
    const auto loot_radius = source.Require<double>("looter.loot_radius_m");
    const auto pickup_range = source.Require<double>("looter.pickup_range_m");
    const auto pickup_retry = source.Require<std::uint32_t>("looter.pickup_retry_ms");
    const auto dwell = source.Require<std::uint32_t>("traveler.dwell_ms");
    const auto travel_retry = source.Require<std::uint32_t>("traveler.request_retry_ms");
    const auto portal_approach = source.Require<double>("traveler.portal_approach_m");
    const auto pair_radius = source.Require<double>("trader.pair_radius_m");
    const auto open_range = source.Require<double>("trader.open_range_m");
    const auto offer_gold = source.Require<std::uint64_t>("trader.offer_gold");
    const auto trade_retry = source.Require<std::uint32_t>("trader.request_retry_ms");
    const auto give_up = source.Require<std::uint32_t>("trader.give_up_ms");
    const auto cooldown = source.Require<std::uint32_t>("trader.cooldown_ms");
    if (auto err = common::FirstConfigError(
            backend, world, class_id, wanderers, fighters, looters, travelers, traders, seed,
            prefix, identity_seed, password, duration, tick_hz, ping, connect, arrive, retarget,
            idle_min, idle_max, margin, min_seeing, min_kills, min_pickups, min_zone_changes,
            min_settlements, engage, attack_skill, attack_range, attack_interval, loot_radius,
            pickup_range, pickup_retry, dwell, travel_retry, portal_approach, pair_radius,
            open_range, offer_gold, trade_retry, give_up, cooldown)) {
        return std::unexpected(*err);
    }
    // Summed in 64 bits so that huge role counts cannot wrap around to a valid-looking total.
    const std::uint64_t bots =
        std::uint64_t{*wanderers} + *fighters + *looters + *travelers + *traders;
    if (bots == 0 || bots > std::numeric_limits<std::uint32_t>::max() || *tick_hz == 0 ||
        *duration == 0) {
        return std::unexpected(common::ConfigError{
            "at least one bot, client.tick_hz and run.duration_sec must be positive"});
    }
    if (*ping == 0 || *connect == 0 || *attack_interval == 0 || *retarget == 0 ||
        *pickup_retry == 0 || *travel_retry == 0 || *trade_retry == 0 || *give_up == 0) {
        return std::unexpected(common::ConfigError{
            "client intervals, fighter.attack_interval_ms, looter.pickup_retry_ms, "
            "traveler.request_retry_ms, trader.request_retry_ms, trader.give_up_ms and "
            "wanderer.retarget_after_ticks must be positive"});
    }
    // Written so that NaN fails too.
    const auto positive = [](double value) { return value > 0.0; };
    const auto non_negative = [](double value) { return value >= 0.0; };
    if (!positive(*arrive) || !non_negative(*margin) || !positive(*engage) ||
        !positive(*attack_range) || !positive(*loot_radius) || !positive(*pickup_range) ||
        !positive(*portal_approach) || !positive(*pair_radius) || !positive(*open_range)) {
        return std::unexpected(
            common::ConfigError{"bot distances must be positive (wanderer.margin_m may be zero)"});
    }
    if (*idle_min > *idle_max) {
        return std::unexpected(
            common::ConfigError{"wanderer.idle_ticks_min must not exceed idle_ticks_max"});
    }

    const auto gateway_host = source.Optional<std::string>("gateway.host");
    const auto gateway_port = source.Optional<std::uint16_t>("gateway.port");
    if (auto err = common::FirstConfigError(gateway_host, gateway_port)) {
        return std::unexpected(*err);
    }
    if ((*gateway_host && (*gateway_host)->empty()) || (*gateway_port && **gateway_port == 0)) {
        return std::unexpected(common::ConfigError{
            "gateway.host and gateway.port override the Backend's address; leave them out "
            "rather than empty or 0"});
    }

    LoadProfile out;
    out.backend_base_url = *backend;
    out.gateway_host = *gateway_host;
    out.gateway_port = *gateway_port;
    out.world = common::WorldId{*world};
    out.class_id = common::ClassId{*class_id};
    out.wanderers = *wanderers;
    out.fighters = *fighters;
    out.looters = *looters;
    out.travelers = *travelers;
    out.traders = *traders;
    out.seed = *seed;
    out.account_prefix = *prefix;
    out.identity_seed = *identity_seed;
    out.password = *password;
    out.duration = std::chrono::seconds{*duration};
    out.criteria = PassCriteria{.min_bots_seeing_others = *min_seeing,
                                .min_kills = *min_kills,
                                .min_pickups = *min_pickups,
                                .min_zone_changes = *min_zone_changes,
                                .min_trade_settlements = *min_settlements};
    out.client_tick_hz = *tick_hz;
    out.ping_interval = std::chrono::milliseconds{*ping};
    out.connect_timeout = std::chrono::milliseconds{*connect};
    out.wanderer = bot::WandererParams{.arrive_distance_m = static_cast<float>(*arrive),
                                       .retarget_after_ticks = *retarget,
                                       .idle_ticks_min = *idle_min,
                                       .idle_ticks_max = *idle_max,
                                       .margin_m = static_cast<float>(*margin)};
    out.fighter = bot::FighterParams{.engage_radius_m = static_cast<float>(*engage),
                                     .attack_skill = common::SkillId{*attack_skill},
                                     .attack_range_m = static_cast<float>(*attack_range),
                                     .attack_interval = std::chrono::milliseconds{*attack_interval},
                                     .roam = out.wanderer};
    out.looter = bot::LooterParams{.loot_radius_m = static_cast<float>(*loot_radius),
                                   .pickup_range_m = static_cast<float>(*pickup_range),
                                   .pickup_retry = std::chrono::milliseconds{*pickup_retry},
                                   .fight = out.fighter};
    out.traveler = bot::TravelerParams{.dwell = std::chrono::milliseconds{*dwell},
                                       .request_retry = std::chrono::milliseconds{*travel_retry},
                                       .portal_approach_m = static_cast<float>(*portal_approach),
                                       .roam = out.wanderer};
    out.trader = bot::TraderParams{.pair_radius_m = static_cast<float>(*pair_radius),
                                   .open_range_m = static_cast<float>(*open_range),
                                   .offer_gold = *offer_gold,
                                   .request_retry = std::chrono::milliseconds{*trade_retry},
                                   .give_up = std::chrono::milliseconds{*give_up},
                                   .cooldown = std::chrono::milliseconds{*cooldown},
                                   .fight = out.fighter};
    return out;
}

namespace {
// Character names allow 2-16 letters or digits: one letter and 13 hex digits (52 random bits)
// make collisions between bots negligible even across many runs in one database.
constexpr int kNameHexDigits = 13;
constexpr std::uint64_t kNameMask = (std::uint64_t{1} << (4 * kNameHexDigits)) - 1;
}  // namespace

BotIdentity LoadProfile::IdentityOf(std::string_view account_prefix, std::uint64_t identity_seed,
                                    std::uint32_t index) {
    // Both names come from one seeded generator, never from std::hash, whose values differ
    // between standard libraries.
    std::mt19937_64 rng{identity_seed ^ (static_cast<std::uint64_t>(index) << 32U)};
    const std::uint64_t account = rng();
    const std::uint64_t character = rng();
    return BotIdentity{
        .username = std::format("{}_{:08x}", account_prefix, static_cast<std::uint32_t>(account)),
        .character_name = std::format("B{:0{}x}", character & kNameMask, kNameHexDigits)};
}

}  // namespace sa::loadgen
