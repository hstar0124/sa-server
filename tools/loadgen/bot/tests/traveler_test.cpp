#include "sa/bot/traveler.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace sa::bot {
namespace {

constexpr common::Vec2 kMap{100.0F, 100.0F};
constexpr std::uint32_t kServerHz = 10;  // 1000 ms is 10 ticks
constexpr common::ZoneId kMeadow{1};
constexpr common::ZoneId kHighlands{2};
constexpr common::PortalId kEastGate{1};
constexpr TravelerParams kParams{
    .dwell = std::chrono::milliseconds{1000},
    .request_retry = std::chrono::milliseconds{500},
    .portal_approach_m = 1.0F,
    .roam =
        WandererParams{.arrive_distance_m = 1.0F, .retarget_after_ticks = 200, .margin_m = 2.0F}};
const std::vector<SeenPortal> kGates{
    SeenPortal{
        .id = common::PortalId{2}, .position = {5.0F, 50.0F}, .radius_m = 3.0F, .to_zone = {}},
    SeenPortal{
        .id = kEastGate, .position = {95.0F, 50.0F}, .radius_m = 3.0F, .to_zone = kHighlands},
};

Observation See(common::Vec2 self, common::ZoneId zone, std::uint64_t tick,
                const std::vector<SeenPortal>& portals = kGates) {
    return Observation{.self_position = self,
                       .map_size = kMap,
                       .self_hp = 100,
                       .self_max_hp = 100,
                       .visible = {},
                       .portals = portals,
                       .zone = zone,
                       .tick = common::TickIndex{tick},
                       .tick_hz = kServerHz};
}

TEST(Traveler, DwellsThenWalksToTheNearestPortalAndAsksFromNearItsCentre) {
    Traveler t{1, kParams};
    const Intent settle = t.Decide(See({70.0F, 50.0F}, kMeadow, 100));
    EXPECT_FALSE(settle.change_zone.has_value());
    EXPECT_NEAR(settle.direction.Length(), 1.0F, 1e-5F) << "roams while dwelling";

    const Intent heading = t.Decide(See({70.0F, 50.0F}, kMeadow, 110));
    EXPECT_GT(heading.direction.x, 0.99F) << "east: the nearer gate, once the dwell is over";
    EXPECT_FALSE(t.Decide(See({93.0F, 50.0F}, kMeadow, 111)).change_zone.has_value())
        << "inside the rim is not close enough yet";

    const Intent ask = t.Decide(See({94.5F, 50.0F}, kMeadow, 112));
    EXPECT_EQ(ask.change_zone, kEastGate);
    EXPECT_TRUE(ask.direction.IsZero());
    EXPECT_TRUE(t.waiting());
}

TEST(Traveler, WaitsForTheNextZoneAndAsksAgainOnlyAfterTheRetryInterval) {
    Traveler t{1, kParams};
    (void)t.Decide(See({94.5F, 50.0F}, kMeadow, 0));
    ASSERT_EQ(t.Decide(See({94.5F, 50.0F}, kMeadow, 10)).change_zone, kEastGate);
    const Intent waiting = t.Decide(See({94.5F, 50.0F}, kMeadow, 14));
    EXPECT_FALSE(waiting.change_zone.has_value());
    EXPECT_TRUE(waiting.direction.IsZero()) << "stands in the portal";
    EXPECT_EQ(t.Decide(See({94.5F, 50.0F}, kMeadow, 15)).change_zone, kEastGate)
        << "500 ms later the zone has not moved it: ask again";

    // The highlands count their own ticks, lower than the meadow's.
    const Intent arrived = t.Decide(See({8.0F, 90.0F}, kHighlands, 3, {}));
    EXPECT_EQ(t.crossings(), 1U);
    EXPECT_FALSE(t.waiting());
    EXPECT_FALSE(arrived.change_zone.has_value());
    EXPECT_NEAR(arrived.direction.Length(), 1.0F, 1e-5F) << "dwells by the new zone's clock";
}

TEST(Traveler, WithoutPortalsOrAClockItRoamsOrWaitsAndTheDeadStandStill) {
    Traveler t{1, kParams};
    Observation unwelcomed = See({50.0F, 50.0F}, {}, 0);
    unwelcomed.tick_hz = 0;
    EXPECT_TRUE(t.Decide(unwelcomed).direction.IsZero());

    (void)t.Decide(See({50.0F, 50.0F}, kMeadow, 0, {}));
    const Intent roaming = t.Decide(See({50.0F, 50.0F}, kMeadow, 50, {}));
    EXPECT_FALSE(roaming.change_zone.has_value());
    EXPECT_NEAR(roaming.direction.Length(), 1.0F, 1e-5F);

    Observation dead = See({94.5F, 50.0F}, kMeadow, 60);
    dead.self_hp = 0;
    const Intent still = t.Decide(dead);
    EXPECT_TRUE(still.direction.IsZero());
    EXPECT_FALSE(still.change_zone.has_value());
    EXPECT_EQ(t.crossings(), 0U);
}

}  // namespace
}  // namespace sa::bot
