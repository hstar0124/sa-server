#include "sa/zone/sim/zone.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace sa::zone::sim {
namespace {

using common::EntityId;
using common::RequestId;
using common::Vec2;

// Player next to the slime at (70,70): 2 m away, inside strike range and inside the slime's aggro.
constexpr Vec2 kBesideSlime{68.0F, 70.0F};
constexpr Vec2 kFarFromSlime{20.0F, 20.0F};

std::vector<CommandResultEvent> ResultsFor(const std::vector<DeliveredEvent>& events,
                                           EntityId player) {
    std::vector<CommandResultEvent> out;
    for (const DeliveredEvent& e : events) {
        if (e.recipient == player) {
            if (const auto* r = std::get_if<CommandResultEvent>(&e.event)) {
                out.push_back(*r);
            }
        }
    }
    return out;
}

std::vector<CombatEvent> CombatFor(const std::vector<DeliveredEvent>& events, EntityId player) {
    std::vector<CombatEvent> out;
    for (const DeliveredEvent& e : events) {
        if (e.recipient == player) {
            if (const auto* c = std::get_if<CombatEvent>(&e.event)) {
                out.push_back(*c);
            }
        }
    }
    return out;
}

RejectReason CastAndTick(Zone& zone, EntityId player, std::uint64_t request, SkillId skill,
                         SkillTarget target) {
    zone.Submit(CastCommand{
        .player = player, .request_id = RequestId{request}, .skill = skill, .target = target});
    zone.Tick();
    const auto results = ResultsFor(zone.DrainEvents(), player);
    if (results.size() != 1) {
        throw std::logic_error{"expected exactly one command result"};
    }
    return results.front().reason;
}

TEST(ZoneCombat, AProjectileWithoutAUsableDirectionIsRejected) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId p = test::Spawn(zone, 1, kFarFromSlime);

    // Normalising an infinite vector yields zero: such a projectile would never travel or end.
    EXPECT_EQ(
        CastAndTick(zone, p, 1, test::kArrow, Vec2{std::numeric_limits<float>::infinity(), 0.0F}),
        RejectReason::kInvalidTarget);
    EXPECT_EQ(zone.stats().projectiles, 0U);
}

TEST(ZoneCombat, StrikeHitsAnAdjacentMonsterAndTellsTheAttacker) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId p = test::Spawn(zone, 1, kBesideSlime);
    const EntityId slime = test::OnlyMonster(zone);
    zone.Tick();
    (void)zone.DrainEvents();

    zone.Submit(CastCommand{
        .player = p, .request_id = RequestId{1}, .skill = test::kStrike, .target = slime});
    zone.Tick();
    const auto events = zone.DrainEvents();

    ASSERT_EQ(ResultsFor(events, p).size(), 1U);
    EXPECT_TRUE(ResultsFor(events, p).front().accepted());
    const auto hits = CombatFor(events, p);
    ASSERT_EQ(hits.size(), 1U);
    EXPECT_EQ(hits.front().target, slime);
    EXPECT_EQ(hits.front().damage, 20);
    EXPECT_EQ(hits.front().target_hp, 40);
    EXPECT_FALSE(hits.front().killed);
    EXPECT_EQ(test::VitalsOf(zone, slime).hp, 40);
    EXPECT_EQ(zone.stats().hits, 1U);
}

TEST(ZoneCombat, EveryRejectReasonIsReported) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId near = test::Spawn(zone, 1, kBesideSlime);
    const EntityId far = test::Spawn(zone, 2, kFarFromSlime);
    const EntityId slime = test::OnlyMonster(zone);
    zone.Tick();

    EXPECT_EQ(CastAndTick(zone, near, 1, SkillId{999}, slime), RejectReason::kUnknownSkill);
    EXPECT_EQ(CastAndTick(zone, near, 2, test::kUnlearned, Vec2{1.0F, 0.0F}),
              RejectReason::kNotLearned);
    EXPECT_EQ(CastAndTick(zone, far, 3, test::kStrike, slime), RejectReason::kOutOfRange);
    EXPECT_EQ(CastAndTick(zone, near, 4, test::kStrike, EntityId{4242}),
              RejectReason::kInvalidTarget);
    EXPECT_EQ(CastAndTick(zone, near, 5, test::kStrike, Vec2{1.0F, 0.0F}),
              RejectReason::kInvalidTarget);
    EXPECT_EQ(CastAndTick(zone, near, 6, test::kArrow, Vec2{}), RejectReason::kInvalidTarget);
    EXPECT_EQ(CastAndTick(zone, near, 7, test::kStrike, slime), RejectReason::kNone);
    EXPECT_EQ(CastAndTick(zone, near, 8, test::kStrike, slime), RejectReason::kOnCooldown);
    EXPECT_EQ(zone.stats().casts_rejected, 7U);
}

TEST(ZoneCombat, RetriedRequestAnswersTheSameWithoutASecondHit) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId p = test::Spawn(zone, 1, kBesideSlime);
    const EntityId slime = test::OnlyMonster(zone);
    zone.Tick();

    EXPECT_EQ(CastAndTick(zone, p, 1, test::kStrike, slime), RejectReason::kNone);
    EXPECT_EQ(CastAndTick(zone, p, 1, test::kStrike, slime), RejectReason::kNone) << "same answer";
    EXPECT_EQ(test::VitalsOf(zone, slime).hp, 40) << "hit once";
    EXPECT_EQ(zone.stats().hits, 1U);
}

TEST(ZoneCombat, CastTimeRootsThePlayerThenActivatesAndRechecksRange) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId p = test::Spawn(zone, 1, kBesideSlime);
    const EntityId slime = test::OnlyMonster(zone);
    zone.Tick();

    zone.Submit(CastCommand{
        .player = p, .request_id = RequestId{1}, .skill = test::kHeavyBlow, .target = slime});
    zone.Submit(MoveCommand{.player = p, .input_seq = 1, .direction = {-1.0F, 0.0F}});
    zone.Tick();  // accepted, MP paid, rooted; 600 ms at 10 Hz = 6 ticks
    EXPECT_EQ(test::VitalsOf(zone, p).mp, 36) << "50 - 15, plus one tick of regen at 10/s";
    EXPECT_EQ(test::PosOf(zone, p), kBesideSlime) << "rooted while casting";
    (void)zone.DrainEvents();  // the heavy blow's own result
    EXPECT_EQ(CastAndTick(zone, p, 2, test::kStrike, slime), RejectReason::kAlreadyCasting);
    test::TickN(zone, 4);
    EXPECT_EQ(test::VitalsOf(zone, slime).hp, 60) << "not resolved yet";
    zone.Tick();
    EXPECT_TRUE(zone.LiveMonsters().empty()) << "60 damage lands on resolve and kills the slime";
    test::TickN(zone, 2);
    EXPECT_LT(test::PosOf(zone, p).x, kBesideSlime.x) << "kept move intent applies after the cast";
}

TEST(ZoneCombat, NotEnoughMpIsRejected) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId pauper = test::Spawn(zone, 1, kBesideSlime, test::kPauper);
    const EntityId slime = test::OnlyMonster(zone);
    zone.Tick();
    EXPECT_EQ(CastAndTick(zone, pauper, 1, test::kHeavyBlow, slime), RejectReason::kNotEnoughMp);
    EXPECT_EQ(test::VitalsOf(zone, pauper).mp, 10) << "nothing was charged";
}

TEST(ZoneCombat, MpIsPaidAtCastStartAndRegeneratesPerTick) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId p = test::Spawn(zone, 1, kBesideSlime);
    const EntityId slime = test::OnlyMonster(zone);
    zone.Tick();
    zone.Submit(CastCommand{.player = p,
                            .request_id = RequestId{1},
                            .skill = test::kArrow,
                            .target = Vec2{1.0F, 0.0F}});
    zone.Submit(CastCommand{
        .player = p, .request_id = RequestId{2}, .skill = test::kHeavyBlow, .target = slime});
    zone.Tick();  // arrow 5 + heavy 15 paid, then one point of regen (10/s at 10 Hz)
    EXPECT_EQ(test::VitalsOf(zone, p).mp, 31);
    EXPECT_EQ(zone.stats().casts_accepted, 2U);
    test::TickN(zone, 10);
    EXPECT_EQ(test::VitalsOf(zone, p).mp, 41);
    test::TickN(zone, 20);
    EXPECT_EQ(test::VitalsOf(zone, p).mp, 50) << "capped at max";
}

TEST(ZoneCombat, ProjectileFliesHitsTheFirstMonsterAndDisappears) {
    Zone zone = test::MakeZone(test::FieldWithSlime({30.0F, 20.0F}));
    const EntityId p = test::Spawn(zone, 1, Vec2{20.0F, 20.0F});
    const EntityId slime = test::OnlyMonster(zone);
    zone.Tick();
    (void)zone.DrainEvents();

    zone.Submit(CastCommand{.player = p,
                            .request_id = RequestId{1},
                            .skill = test::kArrow,
                            .target = Vec2{1.0F, 0.0F}});
    zone.Tick();
    EXPECT_EQ(zone.stats().projectiles, 1U) << "in flight (25 m/s, 10 m away = 4 ticks)";
    test::TickN(zone, 4);
    EXPECT_EQ(zone.stats().projectiles, 0U);
    EXPECT_EQ(test::VitalsOf(zone, slime).hp, 35);
    const auto hits = CombatFor(zone.DrainEvents(), p);
    ASSERT_FALSE(hits.empty());
    EXPECT_EQ(hits.back().skill, test::kArrow);
}

TEST(ZoneCombat, ProjectileThatMissesExpiresAtItsRange) {
    Zone zone = test::MakeZone();
    const EntityId p = test::Spawn(zone, 1, Vec2{20.0F, 20.0F});
    zone.Tick();
    zone.Submit(CastCommand{.player = p,
                            .request_id = RequestId{1},
                            .skill = test::kArrow,
                            .target = Vec2{0.0F, 1.0F}});
    zone.Tick();
    EXPECT_EQ(zone.stats().projectiles, 1U);
    test::TickN(zone, 12);  // 30 m at 2.5 m per tick
    EXPECT_EQ(zone.stats().projectiles, 0U);
    EXPECT_EQ(zone.stats().hits, 0U);
}

TEST(ZoneCombat, ObserversSeeTheHitAndTheMonsterVanishOnDeath) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId attacker = test::Spawn(zone, 1, kBesideSlime);
    const EntityId watcher = test::Spawn(zone, 2, Vec2{60.0F, 60.0F});  // sees both
    const EntityId slime = test::OnlyMonster(zone);
    zone.Tick();
    (void)zone.CollectView(watcher);
    (void)zone.DrainEvents();

    for (std::uint64_t i = 1; i <= 3; ++i) {  // 3 strikes of 20 = dead
        zone.Submit(CastCommand{.player = attacker,
                                .request_id = RequestId{i},
                                .skill = test::kStrike,
                                .target = slime});
        zone.Tick();
        test::TickN(zone, 8);  // cooldown 800 ms
    }
    auto seen = CombatFor(zone.DrainEvents(), watcher);
    std::erase_if(seen, [&](const CombatEvent& e) {
        return e.attacker != attacker;
    });  // the slime hits back too
    ASSERT_EQ(seen.size(), 3U);
    EXPECT_TRUE(seen.back().killed);
    const PlayerView view = zone.CollectView(watcher);
    EXPECT_EQ(view.despawned, std::vector{slime});
    EXPECT_EQ(zone.stats().kills, 1U);
}

TEST(ZoneCombat, DeadPlayerCannotActAndRespawnsWithFullVitals) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId p = test::Spawn(zone, 1, kBesideSlime);
    const EntityId slime = test::OnlyMonster(zone);
    // The slime closes in and swings every second for 8: 200 hp is gone within 250 ticks.
    test::TickN(zone, 250);
    EXPECT_EQ(test::VitalsOf(zone, p).hp, 0) << "killed by the slime";
    EXPECT_EQ(CastAndTick(zone, p, 1, test::kStrike, slime), RejectReason::kDead);
    zone.Submit(MoveCommand{.player = p, .input_seq = 1, .direction = {1.0F, 0.0F}});
    zone.Tick();
    EXPECT_EQ(test::PosOf(zone, p), kBesideSlime) << "the dead do not walk";

    test::TickN(zone, 12);  // respawn 1 s after death
    EXPECT_EQ(test::VitalsOf(zone, p).hp, 200);
    const auto& spawns = zone.map().spawn_points;
    EXPECT_TRUE(std::ranges::find(spawns, test::PosOf(zone, p)) != spawns.end());
}

TEST(ZoneCombat, CombatIsDeterministicForTheSameSeedAndInputs) {
    auto run = [] {
        Zone zone = test::MakeZone(test::FieldWithSlime(), test::Rules(), 7);
        const EntityId p = test::Spawn(zone, 1, kBesideSlime);
        const EntityId slime = test::OnlyMonster(zone);
        std::uint64_t request = 1;
        for (int t = 0; t < 120; ++t) {
            if (t % 9 == 0) {
                zone.Submit(CastCommand{.player = p,
                                        .request_id = RequestId{request++},
                                        .skill = test::kStrike,
                                        .target = slime});
            }
            zone.Tick();
        }
        return std::pair{test::VitalsOf(zone, p).hp, zone.stats().kills};
    };
    EXPECT_EQ(run(), run());
}

// Two archers 10 m either side of the slime (outside its aggro radius, so it never moves). An
// arrow travels 2.5 m per tick at 10 Hz and both arrive on the fourth tick.
constexpr Vec2 kSouthArcher{70.0F, 60.0F};
constexpr Vec2 kNorthArcher{70.0F, 80.0F};
constexpr int kArrowFlightTicks = 5;
constexpr int kArrowCooldownTicks = 10;

std::vector<KillRecord> KillsIn(const std::vector<HostRequest>& requests) {
    std::vector<KillRecord> kills;
    for (const HostRequest& request : requests) {
        if (const auto* kill = std::get_if<KillRecord>(&request)) {
            kills.push_back(*kill);
        }
    }
    return kills;
}

// One arrow from each side: the slime (60 hp) is left with 10 hp, less than one arrow.
void WoundSlime(Zone& zone, EntityId south, EntityId north) {
    zone.Submit(CastCommand{.player = south,
                            .request_id = RequestId{1},
                            .skill = test::kArrow,
                            .target = Vec2{0.0F, 1.0F}});
    zone.Submit(CastCommand{.player = north,
                            .request_id = RequestId{1},
                            .skill = test::kArrow,
                            .target = Vec2{0.0F, -1.0F}});
    test::TickN(zone, kArrowCooldownTicks);
}

TEST(ZoneCombat, TwoArrowsKillingInOneTickRecordOneKillForTheOlderArrow) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId south = test::Spawn(zone, 1, kSouthArcher);
    const EntityId north = test::Spawn(zone, 2, kNorthArcher);
    const EntityId slime = test::OnlyMonster(zone);
    WoundSlime(zone, south, north);
    ASSERT_EQ(test::VitalsOf(zone, slime).hp, 10);

    // North fires first, so its arrow is the older entity; both land on the same tick.
    zone.Submit(CastCommand{.player = north,
                            .request_id = RequestId{2},
                            .skill = test::kArrow,
                            .target = Vec2{0.0F, -1.0F}});
    zone.Submit(CastCommand{.player = south,
                            .request_id = RequestId{2},
                            .skill = test::kArrow,
                            .target = Vec2{0.0F, 1.0F}});
    test::TickN(zone, kArrowFlightTicks);

    EXPECT_TRUE(zone.LiveMonsters().empty());
    const auto kills = KillsIn(zone.DrainHostRequests());
    ASSERT_EQ(kills.size(), 1U) << "death is processed once";
    EXPECT_EQ(kills.front().killer, north) << "the older arrow lands first";
    EXPECT_EQ(zone.stats().kills, 1U);
}

TEST(ZoneCombat, AnArrowWhoseArcherLeftKillsButEarnsNoKillRecord) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId south = test::Spawn(zone, 1, kSouthArcher);
    const EntityId north = test::Spawn(zone, 2, kNorthArcher);
    WoundSlime(zone, south, north);

    zone.Submit(CastCommand{.player = south,
                            .request_id = RequestId{2},
                            .skill = test::kArrow,
                            .target = Vec2{0.0F, 1.0F}});
    zone.Tick();  // the arrow is in flight
    zone.DespawnPlayer(south);
    test::TickN(zone, kArrowFlightTicks);

    EXPECT_TRUE(zone.LiveMonsters().empty()) << "the arrow still lands";
    EXPECT_TRUE(KillsIn(zone.DrainHostRequests()).empty())
        << "no character to credit, so nothing for the Backend to record";
}

}  // namespace
}  // namespace sa::zone::sim
