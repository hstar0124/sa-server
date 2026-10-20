#include "sa/bot/fighter.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace sa::bot {
namespace {

constexpr common::Vec2 kMap{100.0F, 100.0F};
constexpr FighterParams kParams{.engage_radius_m = 30.0F,
                                .attack_skill_id = 1,
                                .attack_range_m = 2.5F,
                                .attack_interval = std::chrono::milliseconds{1000}};
constexpr std::uint32_t kServerHz = 10;  // 1000 ms at 10 Hz: one swing every 10 ticks

Observation See(common::Vec2 self, const std::vector<SeenEntity>& visible, std::uint64_t tick = 0) {
    return Observation{.self_position = self,
                       .map_size = kMap,
                       .self_hp = 100,
                       .self_max_hp = 100,
                       .visible = visible,
                       .tick = tick,
                       .tick_hz = kServerHz};
}

TEST(Fighter, RoamsWhenNothingIsInSight) {
    Fighter f{1, kParams};
    const Intent intent = f.Decide(See({50.0F, 50.0F}, {}));
    EXPECT_FALSE(intent.cast.has_value());
    EXPECT_NEAR(intent.direction.Length(), 1.0F, 1e-5F) << "wandering";
    EXPECT_FALSE(f.target().has_value());
}

TEST(Fighter, WalksTowardTheNearestLiveMonsterThenSwingsOnATimer) {
    Fighter f{1, kParams};
    const std::vector<SeenEntity> visible{
        SeenEntity{.id = common::EntityId{7},
                   .kind = SeenKind::kMonster,
                   .position = {60.0F, 50.0F},
                   .hp = 10},
        SeenEntity{.id = common::EntityId{8},
                   .kind = SeenKind::kMonster,
                   .position = {55.0F, 50.0F},
                   .hp = 0},  // dead
        SeenEntity{.id = common::EntityId{9},
                   .kind = SeenKind::kPlayer,
                   .position = {51.0F, 50.0F},
                   .hp = 100},
    };
    const Intent approach = f.Decide(See({50.0F, 50.0F}, visible, 1));
    EXPECT_EQ(f.target(), common::EntityId{7})
        << "nearest live monster, not the corpse or the player";
    EXPECT_GT(approach.direction.x, 0.99F);
    EXPECT_FALSE(approach.cast.has_value());

    const Intent swing = f.Decide(See({58.0F, 50.0F}, visible, 2));
    const CastIntent cast = swing.cast.value_or(CastIntent{});
    EXPECT_EQ(cast.skill_id, 1U);
    EXPECT_EQ(std::get<common::EntityId>(cast.target), common::EntityId{7});
    EXPECT_TRUE(swing.direction.IsZero()) << "stands still in range";

    const Intent wait = f.Decide(See({58.0F, 50.0F}, visible, 5));
    EXPECT_FALSE(wait.cast.has_value()) << "interval not elapsed";
    const Intent again = f.Decide(See({58.0F, 50.0F}, visible, 12));
    EXPECT_TRUE(again.cast.has_value());
    EXPECT_EQ(f.casts(), 2U);
}

TEST(Fighter, IgnoresMonstersBeyondEngageRadiusAndWaitsWhileDead) {
    Fighter f{1, kParams};
    const std::vector<SeenEntity> far{SeenEntity{.id = common::EntityId{7},
                                                 .kind = SeenKind::kMonster,
                                                 .position = {90.0F, 90.0F},
                                                 .hp = 10}};
    EXPECT_FALSE(f.Decide(See({10.0F, 10.0F}, far)).cast.has_value());
    EXPECT_FALSE(f.target().has_value());

    Observation dead = See({10.0F, 10.0F}, far);
    dead.self_hp = 0;
    const Intent intent = f.Decide(dead);
    EXPECT_TRUE(intent.direction.IsZero());
    EXPECT_FALSE(intent.cast.has_value());
}

TEST(Fighter, TheIntervalFollowsTheServersTickRateNotTheClients) {
    Fighter f{1, kParams};
    const std::vector<SeenEntity> beside{SeenEntity{.id = common::EntityId{7},
                                                    .kind = SeenKind::kMonster,
                                                    .position = {51.0F, 50.0F},
                                                    .hp = 10}};
    Observation at_20hz = See({50.0F, 50.0F}, beside, 1);
    at_20hz.tick_hz = 20;
    EXPECT_TRUE(f.Decide(at_20hz).cast.has_value());
    at_20hz.tick = 15;
    EXPECT_FALSE(f.Decide(at_20hz).cast.has_value()) << "1000 ms is 20 ticks at 20 Hz";
    at_20hz.tick = 21;
    EXPECT_TRUE(f.Decide(at_20hz).cast.has_value());

    Fighter waiting{2, kParams};
    Observation unknown_rate = See({50.0F, 50.0F}, beside, 1);
    unknown_rate.tick_hz = 0;
    EXPECT_FALSE(waiting.Decide(unknown_rate).cast.has_value())
        << "no swing before the server's rate is known";
}

}  // namespace
}  // namespace sa::bot
