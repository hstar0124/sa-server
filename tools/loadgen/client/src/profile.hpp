#pragma once

#include "sa/bot/fighter.hpp"
#include "sa/bot/looter.hpp"
#include "sa/bot/traveler.hpp"
#include "sa/bot/wanderer.hpp"
#include "sa/common/config_source.hpp"
#include "sa/common/ids.hpp"

#include "report.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace sa::loadgen {

// One run of the bot client. Every input the bots send derives from `seed` (ADR-0006).
// Account and character names of one bot; the same identity seed and index give the same names.
struct BotIdentity {
    std::string username;
    std::string character_name;
};

// Bots are numbered wanderers first, then fighters, looters and travelers.
enum class BotRole : std::uint8_t { kWanderer, kFighter, kLooter, kTraveler };

struct LoadProfile {
    std::string backend_base_url;  // e.g. http://localhost:5100
    // Override the gateway address the Backend hands out (absent: use the Backend's).
    std::optional<std::string> gateway_host;
    std::optional<std::uint16_t> gateway_port;
    common::WorldId world;
    common::ClassId class_id;

    std::uint32_t wanderers = 0;  // bot roles, numbered in this order (see RoleOf)
    std::uint32_t fighters = 0;
    std::uint32_t looters = 0;
    std::uint32_t travelers = 0;
    std::uint64_t seed = 0;
    // Where the names come from, apart from `seed`: a character keeps what earlier runs left
    // (inventory, last position), so a run normally gets fresh ones. Absent: a seed from the OS;
    // pinned, a run reuses the accounts of an earlier one on purpose.
    std::optional<std::uint64_t> identity_seed;
    std::string account_prefix;  // usernames are <prefix>_<identity-derived hex>
    std::string password;

    std::chrono::seconds duration{};
    PassCriteria criteria;
    std::uint32_t client_tick_hz = 0;
    std::chrono::milliseconds ping_interval{};
    std::chrono::milliseconds connect_timeout{};
    bot::WandererParams wanderer;
    bot::FighterParams fighter;
    bot::LooterParams looter;
    bot::TravelerParams traveler;

    [[nodiscard]] std::uint32_t bot_count() const noexcept {
        return wanderers + fighters + looters + travelers;
    }
    [[nodiscard]] BotRole RoleOf(std::uint32_t index) const noexcept {
        if (index < wanderers) {
            return BotRole::kWanderer;
        }
        if (index < wanderers + fighters) {
            return BotRole::kFighter;
        }
        return index < wanderers + fighters + looters ? BotRole::kLooter : BotRole::kTraveler;
    }
    // Deterministic per-bot identities for one identity seed.
    [[nodiscard]] static BotIdentity IdentityOf(std::string_view account_prefix,
                                                std::uint64_t identity_seed, std::uint32_t index);

    static constexpr std::string_view kEnvPrefix = "SA_LOADGEN__";

    [[nodiscard]] static std::expected<LoadProfile, common::ConfigError> FromConfig(
        const common::ConfigSource& source);
};

}  // namespace sa::loadgen
