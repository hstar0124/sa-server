#include "sa/bot/looter.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace sa::bot {
namespace {

constexpr common::Vec2 kMap{100.0F, 100.0F};
constexpr std::uint32_t kServerHz = 10;  // 1000 ms is 10 ticks
constexpr LooterParams kParams{
    .loot_radius_m = 20.0F,
    .pickup_range_m = 1.5F,
    .pickup_retry = std::chrono::milliseconds{1000},
    .fight = FighterParams{
        .engage_radius_m = 30.0F,
        .attack_skill = common::SkillId{1},
        .attack_range_m = 2.5F,
        .attack_interval = std::chrono::milliseconds{1000},
        .roam = WandererParams{
            .arrive_distance_m = 1.0F, .retarget_after_ticks = 200, .margin_m = 2.0F}}};

Observation See(common::Vec2 self, const std::vector<SeenEntity>& visible, std::uint64_t tick = 0,
                std::int32_t hp = 100) {
    return Observation{.self_position = self,
                       .map_size = kMap,
                       .self_hp = hp,
                       .self_max_hp = 100,
                       .visible = visible,
                       .tick = common::TickIndex{tick},
                       .tick_hz = kServerHz};
}

const std::vector<SeenEntity> kGelAndMonster{
    SeenEntity{.id = common::EntityId{20}, .kind = SeenKind::kItem, .position = {60.0F, 50.0F}},
    SeenEntity{.id = common::EntityId{21}, .kind = SeenKind::kItem, .position = {90.0F, 50.0F}},
    SeenEntity{.id = common::EntityId{7},
               .kind = SeenKind::kMonster,
               .position = {52.0F, 50.0F},
               .hp = 10},
};

TEST(Looter, WalksToTheNearestItemInSightBeforeFighting) {
    Looter l{1, kParams};
    const Intent intent = l.Decide(See({50.0F, 50.0F}, kGelAndMonster, 1));
    EXPECT_EQ(l.target(), common::EntityId{20}) << "the near item, not the far one";
    EXPECT_GT(intent.direction.x, 0.99F);
    EXPECT_FALSE(intent.cast.has_value()) << "the monster next to it can wait";
    EXPECT_FALSE(intent.pickup.has_value());
}

TEST(Looter, AsksOnceInRangeAndAgainOnlyAfterTheRetryInterval) {
    Looter l{1, kParams};
    const Intent first = l.Decide(See({59.0F, 50.0F}, kGelAndMonster, 5));
    EXPECT_EQ(first.pickup, common::EntityId{20});
    EXPECT_TRUE(first.direction.IsZero());
    EXPECT_FALSE(l.Decide(See({59.0F, 50.0F}, kGelAndMonster, 9)).pickup.has_value())
        << "waiting for the zone's answer";
    EXPECT_EQ(l.Decide(See({59.0F, 50.0F}, kGelAndMonster, 15)).pickup, common::EntityId{20})
        << "still lying there: ask again";
    EXPECT_EQ(l.pickups(), 2U);
}

TEST(Looter, FightsWhenNothingLiesAroundAndWaitsWhileDead) {
    Looter l{1, kParams};
    const std::vector<SeenEntity> monster_only{kGelAndMonster.back()};
    const Intent fight = l.Decide(See({51.0F, 50.0F}, monster_only, 1));
    EXPECT_TRUE(fight.cast.has_value()) << "in range of the monster: swing";
    EXPECT_FALSE(l.target().has_value());

    const Intent dead = l.Decide(See({59.0F, 50.0F}, kGelAndMonster, 2, 0));
    EXPECT_FALSE(dead.pickup.has_value());
    EXPECT_FALSE(dead.cast.has_value());
    EXPECT_TRUE(dead.direction.IsZero());
}

TEST(Looter, IgnoresItemsBeyondTheLootRadius) {
    Looter l{1, kParams};
    const std::vector<SeenEntity> far_item{kGelAndMonster[1]};
    const Intent intent = l.Decide(See({50.0F, 50.0F}, far_item, 1));
    EXPECT_FALSE(l.target().has_value());
    EXPECT_FALSE(intent.pickup.has_value());
    EXPECT_NEAR(intent.direction.Length(), 1.0F, 1e-5F) << "roams like a fighter with no monster";
}

}  // namespace
}  // namespace sa::bot
