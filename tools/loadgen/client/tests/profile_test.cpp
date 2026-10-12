#include "profile.hpp"

#include <gtest/gtest.h>

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
    EXPECT_EQ(profile->fighter.attack_interval_ticks, 18U) << "900 ms at 20 Hz";
    EXPECT_EQ(profile->seed, 7U);
    EXPECT_TRUE(profile->gateway_host.empty());
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

}  // namespace
}  // namespace sa::loadgen
