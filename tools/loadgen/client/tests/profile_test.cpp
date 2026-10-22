#include "profile.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

namespace sa::loadgen {
namespace {

constexpr std::string_view kProfile = R"(
[backend]
base_url = "http://localhost:5100"
[world]
id = 1
class_id = 1
[bots]
wanderers = 1
fighters = 2
looters = 1
travelers = 1
seed = 7
account_prefix = "bot"
password = "pw"
[run]
duration_sec = 5
min_bots_seeing_others = 1
min_kills = 0
min_pickups = 0
min_zone_changes = 0
[client]
tick_hz = 20
ping_interval_ms = 1000
connect_timeout_ms = 2000
[wanderer]
arrive_distance_m = 1.0
retarget_after_ticks = 100
idle_ticks_min = 0
idle_ticks_max = 10
margin_m = 2.0
[fighter]
engage_radius_m = 30.0
attack_skill_id = 1
attack_range_m = 2.5
attack_interval_ms = 900
[looter]
loot_radius_m = 20.0
pickup_range_m = 1.5
pickup_retry_ms = 2000
[traveler]
dwell_ms = 5000
request_retry_ms = 2000
portal_approach_m = 1.0
)";

TEST(LoadProfile, ParsesAndDefaultsGatewayOverrideToEmpty) {
    auto source = common::ConfigSource::FromText(kProfile, "");
    ASSERT_TRUE(source.has_value());
    auto profile = LoadProfile::FromConfig(*source);
    ASSERT_TRUE(profile.has_value()) << profile.error().message;
    EXPECT_EQ(profile->bot_count(), 5U);
    EXPECT_EQ(profile->RoleOf(0), BotRole::kWanderer);
    EXPECT_EQ(profile->RoleOf(1), BotRole::kFighter);
    EXPECT_EQ(profile->RoleOf(2), BotRole::kFighter);
    EXPECT_EQ(profile->RoleOf(3), BotRole::kLooter);
    EXPECT_EQ(profile->RoleOf(4), BotRole::kTraveler);
    EXPECT_EQ(profile->traveler.dwell, std::chrono::milliseconds{5000});
    EXPECT_FLOAT_EQ(profile->traveler.portal_approach_m, 1.0F);
    EXPECT_EQ(profile->traveler.roam.margin_m, profile->wanderer.margin_m)
        << "a traveler roams like a wanderer between crossings";
    EXPECT_EQ(profile->looter.pickup_retry, std::chrono::milliseconds{2000});
    EXPECT_EQ(profile->looter.fight.attack_range_m, profile->fighter.attack_range_m)
        << "a looter fights like a fighter";
    EXPECT_EQ(profile->fighter.attack_interval, std::chrono::milliseconds{900})
        << "kept as wall time; the fighter converts it with the server's tick rate";
    EXPECT_EQ(profile->seed, 7U);
    EXPECT_FALSE(profile->identity_seed.has_value()) << "absent: fresh accounts every run";
    EXPECT_FALSE(profile->gateway_host.has_value());
    EXPECT_EQ(profile->duration.count(), 5);
    EXPECT_EQ(profile->wanderer.idle_ticks_max, 10U);
}

TEST(LoadProfile, RejectsZeroBotsAndInvertedIdleRange) {
    std::string zero{kProfile};
    zero.replace(zero.find("wanderers = 1"), 13, "wanderers = 0");
    zero.replace(zero.find("fighters = 2"), 12, "fighters = 0");
    zero.replace(zero.find("looters = 1"), 11, "looters = 0");
    zero.replace(zero.find("travelers = 1"), 13, "travelers = 0");
    EXPECT_FALSE(LoadProfile::FromConfig(*common::ConfigSource::FromText(zero, "")).has_value());

    std::string inverted{kProfile};
    inverted.replace(inverted.find("idle_ticks_min = 0"), 18, "idle_ticks_min = 50");
    EXPECT_FALSE(
        LoadProfile::FromConfig(*common::ConfigSource::FromText(inverted, "")).has_value());
}

TEST(LoadProfile, RejectsNonPositiveLooterSettingsAndRoleCountsThatOverflow) {
    for (const auto& [key, bad] :
         {std::pair{"loot_radius_m = 20.0", "loot_radius_m = 0.0"},
          std::pair{"pickup_range_m = 1.5", "pickup_range_m = nan"},
          std::pair{"pickup_retry_ms = 2000", "pickup_retry_ms = 0"},
          std::pair{"request_retry_ms = 2000", "request_retry_ms = 0"},
          std::pair{"portal_approach_m = 1.0", "portal_approach_m = 0.0"}}) {
        std::string text{kProfile};
        text.replace(text.find(key), std::string_view{key}.size(), bad);
        EXPECT_FALSE(LoadProfile::FromConfig(*common::ConfigSource::FromText(text, "")).has_value())
            << bad;
    }
    std::string wrapping{kProfile};
    wrapping.replace(wrapping.find("wanderers = 1"), 13, "wanderers = 4294967295");
    EXPECT_FALSE(LoadProfile::FromConfig(*common::ConfigSource::FromText(wrapping, "")).has_value())
        << "4294967295 + 2 + 1 + 1 must not wrap around to 3 bots";
}

TEST(LoadProfile, AGatewayOverrideMustBeARealAddressWhenGiven) {
    const std::string overridden = std::string{kProfile} + "[gateway]\nhost = \"\"\nport = 0\n";
    EXPECT_FALSE(
        LoadProfile::FromConfig(*common::ConfigSource::FromText(overridden, "")).has_value())
        << "empty and 0 once meant \"no override\"; absence means that now";
}

// The shipped profiles are what scripts/e2e-local.sh runs; a profile that no longer loads, or
// loads into a different meaning, would only show up there.
TEST(LoadProfile, TheShippedProfilesLoadWithoutAGatewayOverride) {
    for (const char* file : {SA_PROFILE_DIR "/smoke.toml", SA_PROFILE_DIR "/fighters.toml",
                             SA_PROFILE_DIR "/looters.toml", SA_PROFILE_DIR "/travelers.toml"}) {
        auto source = common::ConfigSource::Load(file, LoadProfile::kEnvPrefix);
        ASSERT_TRUE(source.has_value()) << file;
        const auto profile = LoadProfile::FromConfig(*source);
        ASSERT_TRUE(profile.has_value()) << file << ": " << profile.error().message;
        EXPECT_FALSE(profile->gateway_host.has_value()) << file;
        EXPECT_FALSE(profile->gateway_port.has_value()) << file;
    }
}

TEST(LoadProfile, BotIdentitiesAreStablePerIdentitySeedAndValidNames) {
    const BotIdentity first = LoadProfile::IdentityOf("bot", 7, 0);
    EXPECT_EQ(LoadProfile::IdentityOf("bot", 7, 0).username, first.username)
        << "same identity seed and index";
    EXPECT_EQ(LoadProfile::IdentityOf("bot", 7, 0).character_name, first.character_name);
    EXPECT_NE(LoadProfile::IdentityOf("bot", 7, 1).character_name, first.character_name);
    EXPECT_TRUE(first.username.starts_with("bot_"));
    // The Backend's name rule: 2-16 letters or digits.
    EXPECT_GE(first.character_name.size(), 2U);
    EXPECT_LE(first.character_name.size(), 16U);
    EXPECT_TRUE(std::ranges::all_of(first.character_name, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }));
    EXPECT_NE(LoadProfile::IdentityOf("bot", 8, 0).username, first.username)
        << "another identity seed, other accounts";
}

}  // namespace
}  // namespace sa::loadgen
