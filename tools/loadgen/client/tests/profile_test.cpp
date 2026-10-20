#include "profile.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>

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
seed = 7
account_prefix = "bot"
password = "pw"
[run]
duration_sec = 5
min_bots_seeing_others = 1
min_kills = 0
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
)";

TEST(LoadProfile, ParsesAndDefaultsGatewayOverrideToEmpty) {
    auto source = common::ConfigSource::FromText(kProfile, "");
    ASSERT_TRUE(source.has_value());
    auto profile = LoadProfile::FromConfig(*source);
    ASSERT_TRUE(profile.has_value()) << profile.error().message;
    EXPECT_EQ(profile->bot_count(), 3U);
    EXPECT_FALSE(profile->IsFighter(0));
    EXPECT_TRUE(profile->IsFighter(1));
    EXPECT_EQ(profile->fighter.attack_interval, std::chrono::milliseconds{900})
        << "kept as wall time; the fighter converts it with the server's tick rate";
    EXPECT_EQ(profile->seed, 7U);
    EXPECT_FALSE(profile->gateway_host.has_value());
    EXPECT_EQ(profile->duration.count(), 5);
    EXPECT_EQ(profile->wanderer.idle_ticks_max, 10U);
}

TEST(LoadProfile, RejectsZeroBotsAndInvertedIdleRange) {
    std::string zero{kProfile};
    zero.replace(zero.find("wanderers = 1"), 13, "wanderers = 0");
    zero.replace(zero.find("fighters = 2"), 12, "fighters = 0");
    EXPECT_FALSE(LoadProfile::FromConfig(*common::ConfigSource::FromText(zero, "")).has_value());

    std::string inverted{kProfile};
    inverted.replace(inverted.find("idle_ticks_min = 0"), 18, "idle_ticks_min = 50");
    EXPECT_FALSE(
        LoadProfile::FromConfig(*common::ConfigSource::FromText(inverted, "")).has_value());
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
    for (const char* file : {SA_PROFILE_DIR "/smoke.toml", SA_PROFILE_DIR "/fighters.toml"}) {
        auto source = common::ConfigSource::Load(file, LoadProfile::kEnvPrefix);
        ASSERT_TRUE(source.has_value()) << file;
        const auto profile = LoadProfile::FromConfig(*source);
        ASSERT_TRUE(profile.has_value()) << file << ": " << profile.error().message;
        EXPECT_FALSE(profile->gateway_host.has_value()) << file;
        EXPECT_FALSE(profile->gateway_port.has_value()) << file;
    }
}

TEST(LoadProfile, BotIdentitiesAreStablePerSeedAndValidNames) {
    LoadProfile profile;
    profile.seed = 7;
    profile.account_prefix = "bot";
    const BotIdentity first = profile.IdentityOf(0);
    EXPECT_EQ(profile.IdentityOf(0).username, first.username) << "same seed and index";
    EXPECT_EQ(profile.IdentityOf(0).character_name, first.character_name);
    EXPECT_NE(profile.IdentityOf(1).character_name, first.character_name);
    EXPECT_TRUE(first.username.starts_with("bot_"));
    // The Backend's name rule: 2-16 letters or digits.
    EXPECT_GE(first.character_name.size(), 2U);
    EXPECT_LE(first.character_name.size(), 16U);
    EXPECT_TRUE(std::ranges::all_of(first.character_name, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }));
    profile.seed = 8;
    EXPECT_NE(profile.IdentityOf(0).username, first.username) << "another seed, other accounts";
}

}  // namespace
}  // namespace sa::loadgen
