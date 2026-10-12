#pragma once

#include "sa/bot/fighter.hpp"
#include "sa/bot/wanderer.hpp"
#include "sa/common/config_source.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <string>

namespace sa::loadgen {

// One run of the bot client. Everything random derives from `seed` (ADR-0006).
struct LoadProfile {
    std::string backend_base_url;  // e.g. http://localhost:5100
    std::string gateway_host;      // empty = use the address the Backend hands out
    std::uint16_t gateway_port = 0;
    std::uint64_t world_id = 0;
    std::uint32_t class_id = 0;

    std::uint32_t wanderers = 0;  // bot roles; bot i is a fighter when i >= wanderers
    std::uint32_t fighters = 0;
    std::uint64_t seed = 0;
    std::string account_prefix;  // usernames are <prefix>_<seed-derived hex>
    std::string password;

    std::chrono::seconds duration{};
    // PASS requires at least this many bots to have seen another player. Random walks on a large
    // map may keep a few bots apart within a short run, so the smoke profile asks for fewer than
    // all.
    std::uint32_t min_bots_seeing_others = 0;
    // PASS also requires at least this many monster kills across all bots (0 when no fighters run).
    std::uint32_t min_kills = 0;
    std::uint32_t client_tick_hz = 0;
    std::chrono::milliseconds ping_interval{};
    std::chrono::milliseconds connect_timeout{};
    bot::WandererParams wanderer;
    bot::FighterParams fighter;

    [[nodiscard]] std::uint32_t bot_count() const noexcept { return wanderers + fighters; }
    [[nodiscard]] bool IsFighter(std::uint32_t index) const noexcept { return index >= wanderers; }

    static constexpr std::string_view kEnvPrefix = "SA_LOADGEN__";

    [[nodiscard]] static std::expected<LoadProfile, common::ConfigError> FromConfig(
        const common::ConfigSource& source);
};

}  // namespace sa::loadgen
