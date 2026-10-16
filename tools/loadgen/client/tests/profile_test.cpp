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
count = 3
seed = 7
account_prefix = "bot"
password = "pw"
[run]
duration_sec = 5
min_bots_seeing_others = 1
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
)";

TEST(LoadProfile, ParsesAndDefaultsGatewayOverrideToEmpty) {
    auto source = common::ConfigSource::FromText(kProfile, "");
    ASSERT_TRUE(source.has_value());
    auto profile = LoadProfile::FromConfig(*source);
    ASSERT_TRUE(profile.has_value()) << profile.error().message;
    EXPECT_EQ(profile->bot_count, 3U);
    EXPECT_EQ(profile->seed, 7U);
    EXPECT_TRUE(profile->gateway_host.empty());
    EXPECT_EQ(profile->duration.count(), 5);
    EXPECT_EQ(profile->wanderer.idle_ticks_max, 10U);
}

TEST(LoadProfile, RejectsZeroBotsAndInvertedIdleRange) {
    std::string zero{kProfile};
    zero.replace(zero.find("count = 3"), 9, "count = 0");
    EXPECT_FALSE(LoadProfile::FromConfig(*common::ConfigSource::FromText(zero, "")).has_value());

    std::string inverted{kProfile};
    inverted.replace(inverted.find("idle_ticks_min = 0"), 18, "idle_ticks_min = 50");
    EXPECT_FALSE(
        LoadProfile::FromConfig(*common::ConfigSource::FromText(inverted, "")).has_value());
}

}  // namespace
}  // namespace sa::loadgen
