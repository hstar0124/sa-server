#include "sa/zone/sim/zone.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

namespace sa::zone::sim {
namespace {

using common::CharacterId;
using common::EntityId;
using common::Vec2;

constexpr float kEps = 1e-3F;

TEST(ZoneMovement, PlayerMovesAtMaxSpeedAlongNormalisedDirection) {
    Zone zone = test::MakeZone();  // 10 Hz, 10 m/s -> 1 m per tick
    const EntityId p = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});

    zone.Submit(MoveCommand{.player = p, .input_seq = 1, .direction = {30.0F, 40.0F}});  // not unit
    test::TickN(zone, 5);

    const Vec2 pos = test::PosOf(zone, p);
    EXPECT_NEAR(pos.x, 50.0F + (0.6F * 5.0F), kEps);
    EXPECT_NEAR(pos.y, 50.0F + (0.8F * 5.0F), kEps);
}

TEST(ZoneMovement, ZeroDirectionStops) {
    Zone zone = test::MakeZone();
    const EntityId p = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    zone.Submit(MoveCommand{.player = p, .input_seq = 1, .direction = {1.0F, 0.0F}});
    test::TickN(zone, 3);
    zone.Submit(MoveCommand{.player = p, .input_seq = 2, .direction = {}});
    test::TickN(zone, 3);

    EXPECT_NEAR(test::PosOf(zone, p).x, 53.0F, kEps);
    EXPECT_EQ(test::SnapshotOf(zone, p).velocity, Vec2{});
}

TEST(ZoneMovement, StaleOrDuplicateInputSequenceIsIgnored) {
    Zone zone = test::MakeZone();
    const EntityId p = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});

    zone.Submit(MoveCommand{.player = p, .input_seq = 5, .direction = {1.0F, 0.0F}});
    zone.Submit(MoveCommand{.player = p, .input_seq = 3, .direction = {-1.0F, 0.0F}});  // late
    zone.Submit(MoveCommand{.player = p, .input_seq = 5, .direction = {0.0F, 1.0F}});   // dup
    test::TickN(zone, 1);

    EXPECT_NEAR(test::PosOf(zone, p).x, 51.0F, kEps);
    EXPECT_EQ(zone.stats().stale_inputs_dropped, 2U);
    EXPECT_EQ(zone.CollectView(p).last_processed_input_seq, 5U);
}

TEST(ZoneMovement, CannotLeaveTheMap) {
    Zone zone = test::MakeZone();
    const EntityId p = test::Spawn(zone, 1, Vec2{98.0F, 50.0F});
    zone.Submit(MoveCommand{.player = p, .input_seq = 1, .direction = {1.0F, 0.0F}});
    test::TickN(zone, 10);

    EXPECT_LE(test::PosOf(zone, p).x, 100.0F);
    EXPECT_GE(test::PosOf(zone, p).x, 99.0F);
}

TEST(ZoneMovement, SlidesAlongAWallInsteadOfSticking) {
    Zone zone = test::MakeZone(test::FieldWithWall());  // wall x in [50,52], y in [0,60]
    const EntityId p = test::Spawn(zone, 1, Vec2{45.0F, 30.0F});
    // Diagonal into the wall: x is blocked, y keeps going.
    zone.Submit(MoveCommand{.player = p, .input_seq = 1, .direction = {1.0F, 1.0F}});
    test::TickN(zone, 20);

    const Vec2 pos = test::PosOf(zone, p);
    EXPECT_LT(pos.x, 50.0F) << "never enters the wall";
    EXPECT_GT(pos.y, 40.0F) << "kept sliding upward";
}

TEST(ZoneMovement, SameSeedAndInputsReproduceTheSameState) {
    auto run = [] {
        Zone zone = test::MakeZone(test::FieldWithWall(), test::Rules(), 42);
        constexpr int kPlayers = 5;
        std::vector<EntityId> ids;
        ids.reserve(kPlayers);
        for (int i = 0; i < kPlayers; ++i) {
            ids.push_back(test::SpawnAnywhere(zone, static_cast<std::uint64_t>(i) + 1));
        }
        for (std::uint32_t seq = 1; seq <= 50; ++seq) {
            for (const EntityId id : ids) {
                const float a = zone.rng().NextUnit() * 6.283F;
                zone.Submit(MoveCommand{
                    .player = id, .input_seq = seq, .direction = {std::cos(a), std::sin(a)}});
            }
            zone.Tick();
        }
        std::vector<Vec2> out;
        out.reserve(ids.size());
        for (const EntityId id : ids) {
            out.push_back(test::PosOf(zone, id));
        }
        return out;
    };
    EXPECT_EQ(run(), run());
}

TEST(ZoneMovement, CommandBudgetDefersExcessToTheNextTick) {
    MovementRules rules = test::Rules();
    rules.commands_per_tick_budget = 2;
    Zone zone = test::MakeZone(test::OpenField(), rules);
    const EntityId p = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    for (std::uint32_t seq = 1; seq <= 5; ++seq) {
        zone.Submit(MoveCommand{.player = p, .input_seq = seq, .direction = {1.0F, 0.0F}});
    }
    zone.Tick();
    EXPECT_EQ(zone.stats().commands_applied, 2U);
    EXPECT_EQ(zone.stats().commands_deferred, 3U);
    test::TickN(zone, 2);
    EXPECT_EQ(zone.stats().commands_applied, 5U);
}

TEST(ZoneMovement, SpawnWithoutPositionUsesASpawnPoint) {
    Zone zone = test::MakeZone();
    const EntityId p = test::SpawnAnywhere(zone, 1);
    const Vec2 pos = test::PosOf(zone, p);
    const auto& spawns = zone.map().spawn_points;
    EXPECT_TRUE(std::ranges::find(spawns, pos) != spawns.end());
}

}  // namespace
}  // namespace sa::zone::sim
