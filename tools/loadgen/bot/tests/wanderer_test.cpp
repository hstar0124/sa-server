#include "sa/bot/wanderer.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace sa::bot {
namespace {

constexpr common::Vec2 kMap{100.0F, 100.0F};

// Current target of a brain that the test knows has one; a missing target is a test bug.
common::Vec2 TargetOf(const Wanderer& w) {
    const auto target = w.target();
    if (!target.has_value()) {
        throw std::logic_error{"wanderer has no target"};
    }
    return *target;
}

Observation At(common::Vec2 position, std::uint64_t tick = 0) {
    return Observation{.self_position = position, .map_size = kMap, .visible = {}, .tick = tick};
}

TEST(Wanderer, SameSeedProducesSameTargets) {
    Wanderer a{7, WandererParams{}};
    Wanderer b{7, WandererParams{}};
    for (int i = 0; i < 20; ++i) {
        ASSERT_EQ(a.Decide(At({50.0F, 50.0F})), b.Decide(At({50.0F, 50.0F})));
    }
    EXPECT_EQ(a.target(), b.target());
}

TEST(Wanderer, HeadsTowardItsTargetWithAUnitVector) {
    Wanderer w{1, WandererParams{}};
    const common::Vec2 dir = w.Decide(At({50.0F, 50.0F}));
    ASSERT_TRUE(w.target().has_value());
    EXPECT_NEAR(dir.Length(), 1.0F, 1e-5F);
    const common::Vec2 expected = (TargetOf(w) - common::Vec2{50.0F, 50.0F}).Normalized();
    EXPECT_NEAR(dir.x, expected.x, 1e-5F);
    EXPECT_NEAR(dir.y, expected.y, 1e-5F);
}

TEST(Wanderer, TargetsStayInsideTheMarginAndArrivalPicksANewOne) {
    Wanderer w{3, WandererParams{.margin_m = 10.0F}};
    (void)w.Decide(At({50.0F, 50.0F}));
    const common::Vec2 first = TargetOf(w);
    EXPECT_GE(first.x, 10.0F);
    EXPECT_LE(first.x, 90.0F);
    EXPECT_GE(first.y, 10.0F);
    EXPECT_LE(first.y, 90.0F);

    // Teleport onto the target: the brain registers arrival and stops for this tick.
    EXPECT_EQ(w.Decide(At(first)), common::Vec2{});
    EXPECT_EQ(w.targets_reached(), 1U);
    (void)w.Decide(At(first));
    ASSERT_TRUE(w.target().has_value());
    EXPECT_NE(TargetOf(w), first);
}

TEST(Wanderer, GivesUpOnAnUnreachableTarget) {
    Wanderer w{5, WandererParams{.retarget_after_ticks = 10}};
    (void)w.Decide(At({50.0F, 50.0F}));
    const common::Vec2 first = TargetOf(w);
    for (int i = 0; i < 10; ++i) {
        (void)w.Decide(At({50.0F, 50.0F}));  // never moves
    }
    (void)w.Decide(At({50.0F, 50.0F}));
    EXPECT_EQ(w.targets_reached(), 0U);
    EXPECT_NE(w.target().value_or(first), first);
}

TEST(Wanderer, IdlesAfterArrivalWhenConfigured) {
    Wanderer w{9, WandererParams{.idle_ticks_min = 3, .idle_ticks_max = 3}};
    (void)w.Decide(At({50.0F, 50.0F}));
    const common::Vec2 target = TargetOf(w);
    EXPECT_EQ(w.Decide(At(target)), common::Vec2{});  // arrival tick
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(w.Decide(At(target)), common::Vec2{}) << "idle tick " << i;
    }
    EXPECT_NE(w.Decide(At(target)), common::Vec2{}) << "moving again";
}

}  // namespace
}  // namespace sa::bot
