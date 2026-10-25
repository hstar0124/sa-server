#include "world_model.hpp"

#include <gtest/gtest.h>

namespace sa::loadgen {
namespace {

game::v1::Welcome WelcomeFor(std::uint64_t self) {
    game::v1::Welcome w;
    w.set_entity_id(self);
    w.mutable_map_size()->set_x(100.0F);
    w.mutable_map_size()->set_y(50.0F);
    return w;
}

game::v1::WorldSnapshot Snapshot(std::uint64_t tick, std::uint32_t acked) {
    game::v1::WorldSnapshot s;
    s.set_server_tick(tick);
    s.set_last_processed_input_seq(acked);
    s.mutable_self()->set_entity_id(1);
    s.mutable_self()->mutable_position()->set_x(10.0F);
    return s;
}

TEST(WorldModel, TracksSelfVisibleSetAndMaxVisible) {
    WorldModel model;
    model.OnWelcome(WelcomeFor(1));
    EXPECT_EQ(model.map_size(), (common::Vec2{100.0F, 50.0F}));

    game::v1::WorldSnapshot s1 = Snapshot(10, 3);
    s1.add_spawned()->set_entity_id(2);
    s1.add_spawned()->set_entity_id(3);
    model.OnSnapshot(s1);
    EXPECT_EQ(model.visible_count(), 2U);
    EXPECT_FLOAT_EQ(model.self_position().x, 10.0F);
    EXPECT_EQ(model.last_acked_input(), 3U);

    game::v1::WorldSnapshot s2 = Snapshot(12, 4);
    s2.add_despawned(2);
    auto* moved = s2.add_updated();
    moved->set_entity_id(3);
    moved->mutable_position()->set_y(7.0F);
    model.OnSnapshot(s2);
    EXPECT_EQ(model.visible_count(), 1U);
    EXPECT_EQ(model.max_visible(), 2U);
    EXPECT_FLOAT_EQ(model.Visible().front().position.y, 7.0F);
    EXPECT_EQ(model.integrity().Total(), 0U);
}

TEST(WorldModel, ExtrapolatesWithTheLastVelocityUntilTheNextUpdate) {
    WorldModel model;
    game::v1::Welcome welcome = WelcomeFor(1);
    welcome.set_simulation_hz(20);
    model.OnWelcome(welcome);

    game::v1::WorldSnapshot s1 = Snapshot(100, 0);
    auto* spawn = s1.add_spawned();
    spawn->set_entity_id(2);
    spawn->mutable_position()->set_x(10.0F);
    spawn->mutable_velocity()->set_x(6.0F);  // walking +x at 6 m/s
    model.OnSnapshot(s1);
    EXPECT_FLOAT_EQ(model.Visible().front().position.x, 10.0F);

    // Ten ticks (0.5 s) later and no update for entity 2: the walk went on.
    model.OnSnapshot(Snapshot(110, 0));
    EXPECT_FLOAT_EQ(model.Visible().front().position.x, 13.0F);

    // An update moves the base; a zero velocity holds it there.
    game::v1::WorldSnapshot s3 = Snapshot(120, 0);
    auto* stop = s3.add_updated();
    stop->set_entity_id(2);
    stop->mutable_position()->set_x(15.0F);
    model.OnSnapshot(s3);
    model.OnSnapshot(Snapshot(140, 0));
    EXPECT_FLOAT_EQ(model.Visible().front().position.x, 15.0F);
    EXPECT_EQ(model.integrity().Total(), 0U);
}

TEST(WorldModel, ExposesKindsHealthAndCombatCounters) {
    WorldModel model;
    model.OnWelcome(WelcomeFor(1));
    game::v1::WorldSnapshot s = Snapshot(1, 0);
    auto* slime = s.add_spawned();
    slime->set_entity_id(5);
    slime->set_kind(game::v1::ENTITY_KIND_MONSTER);
    slime->set_hp(60);
    slime->set_max_hp(60);
    s.mutable_self_vitals()->set_hp(150);
    s.mutable_self_vitals()->set_max_hp(200);
    model.OnSnapshot(s);
    ASSERT_EQ(model.Visible().size(), 1U);
    EXPECT_EQ(model.Visible().front().kind, bot::SeenKind::kMonster);
    EXPECT_EQ(model.Visible().front().hp, 60);
    EXPECT_EQ(model.self_hp(), 150);

    game::v1::CommandResult accepted;
    accepted.set_request_id(1);
    accepted.set_status(game::v1::COMMAND_STATUS_ACCEPTED);
    model.ExpectResult(common::RequestId{1}, RequestKind::kCast);
    model.OnCommandResult(accepted);
    game::v1::CombatEvent hit;
    hit.set_attacker_id(1);
    hit.set_target_id(5);
    hit.set_killed(true);
    model.OnCombatEvent(hit);
    game::v1::CombatEvent died;
    died.set_attacker_id(5);
    died.set_target_id(1);
    died.set_killed(true);
    model.OnCombatEvent(died);
    EXPECT_EQ(model.combat().casts_accepted, 1U);
    EXPECT_EQ(model.combat().hits_dealt, 1U);
    EXPECT_EQ(model.combat().kills, 1U);
    EXPECT_EQ(model.combat().deaths, 1U);
}

TEST(WorldModel, CountsEveryKindOfContradiction) {
    WorldModel model;
    model.OnWelcome(WelcomeFor(1));
    game::v1::WorldSnapshot s = Snapshot(5, 2);
    s.add_spawned()->set_entity_id(1);  // self in view
    s.add_spawned()->set_entity_id(9);
    s.add_spawned()->set_entity_id(9);   // duplicate spawn
    s.add_updated()->set_entity_id(77);  // never spawned
    s.add_despawned(55);                 // never spawned
    model.OnSnapshot(s);
    model.OnSnapshot(Snapshot(6, 1));  // ack regressed

    const WorldModel::Integrity& i = model.integrity();
    EXPECT_EQ(i.self_in_view, 1U);
    EXPECT_EQ(i.spawn_of_known, 1U);
    EXPECT_EQ(i.update_of_unknown, 1U);
    EXPECT_EQ(i.despawn_of_unknown, 1U);
    EXPECT_EQ(i.input_seq_regressed, 1U);
    EXPECT_EQ(i.Total(), 5U);
    EXPECT_EQ(model.visible_count(), 1U);
}

TEST(WorldModel, OnlyOtherPlayersCountAsMeetingSomeone) {
    WorldModel model;
    model.OnWelcome(WelcomeFor(1));
    game::v1::WorldSnapshot s = Snapshot(1, 1);
    auto* monster = s.add_spawned();
    monster->set_entity_id(5);
    monster->set_kind(game::v1::ENTITY_KIND_MONSTER);
    auto* item = s.add_spawned();
    item->set_entity_id(6);
    item->set_kind(game::v1::ENTITY_KIND_DROPPED_ITEM);
    model.OnSnapshot(s);
    EXPECT_EQ(model.max_visible(), 2U);
    EXPECT_EQ(model.max_visible_players(), 0U) << "a slime is not another client";

    game::v1::WorldSnapshot t = Snapshot(2, 2);
    auto* player = t.add_spawned();
    player->set_entity_id(7);
    player->set_kind(game::v1::ENTITY_KIND_PLAYER);
    model.OnSnapshot(t);
    EXPECT_EQ(model.max_visible_players(), 1U);
}

game::v1::CommandResult ResultOf(std::uint64_t request, game::v1::CommandStatus status) {
    game::v1::CommandResult r;
    r.set_request_id(request);
    r.set_status(status);
    return r;
}

// PENDING is not a verdict and UNKNOWN is not a refusal: only final answers close a request, and
// each lands under the kind of command its request id was spent on.
TEST(WorldModel, CountsResultsUnderTheRequestKindAndIgnoresPendingOnes) {
    WorldModel model;
    model.OnWelcome(WelcomeFor(1));
    model.ExpectResult(common::RequestId{1}, RequestKind::kCast);
    model.ExpectResult(common::RequestId{2}, RequestKind::kPickup);
    model.ExpectResult(common::RequestId{3}, RequestKind::kPickup);
    model.ExpectResult(common::RequestId{4}, RequestKind::kPickup);
    model.ExpectResult(common::RequestId{5}, RequestKind::kCast);

    model.OnCommandResult(ResultOf(1, game::v1::COMMAND_STATUS_REJECTED));
    model.OnCommandResult(ResultOf(2, game::v1::COMMAND_STATUS_PENDING));
    EXPECT_EQ(model.awaiting_results(), 4U) << "pending keeps the request open";
    model.OnCommandResult(ResultOf(2, game::v1::COMMAND_STATUS_ACCEPTED));
    model.OnCommandResult(ResultOf(3, game::v1::COMMAND_STATUS_REJECTED));
    model.OnCommandResult(ResultOf(4, game::v1::COMMAND_STATUS_UNKNOWN));
    model.OnCommandResult(ResultOf(5, game::v1::COMMAND_STATUS_UNKNOWN));

    EXPECT_EQ(model.combat().casts_rejected, 1U);
    EXPECT_EQ(model.combat().casts_accepted, 0U);
    EXPECT_EQ(model.combat().casts_unknown, 1U);
    EXPECT_EQ(model.loot().pickups_acquired, 1U);
    EXPECT_EQ(model.loot().pickups_rejected, 1U);
    EXPECT_EQ(model.loot().pickups_unknown, 1U);
    EXPECT_EQ(model.awaiting_results(), 0U);
    EXPECT_EQ(model.integrity().Total(), 0U);
}

TEST(WorldModel, AResultForARequestNeverSentOrAlreadyAnsweredIsAContradiction) {
    WorldModel model;
    model.ExpectResult(common::RequestId{1}, RequestKind::kPickup);
    model.OnCommandResult(ResultOf(1, game::v1::COMMAND_STATUS_ACCEPTED));
    model.OnCommandResult(ResultOf(1, game::v1::COMMAND_STATUS_ACCEPTED));
    model.OnCommandResult(ResultOf(99, game::v1::COMMAND_STATUS_REJECTED));
    EXPECT_EQ(model.loot().pickups_acquired, 1U) << "one request, one final answer";
    EXPECT_EQ(model.integrity().result_of_unknown_request, 2U);
}

TEST(WorldModel, MirrorsTheInventoryAndRefusesOlderVersions) {
    WorldModel model;
    game::v1::InventoryUpdate first;
    first.set_version(2);
    first.set_gold(7);
    first.add_items()->set_item_template_id(2001);
    first.add_items()->set_item_template_id(2101);
    model.OnInventoryUpdate(first);
    EXPECT_EQ(model.loot().items_held, 2U);
    EXPECT_EQ(model.loot().gold, 7U);

    game::v1::InventoryUpdate stale;
    stale.set_version(1);
    stale.set_gold(0);
    model.OnInventoryUpdate(stale);
    EXPECT_EQ(model.loot().gold, 7U) << "an older state does not overwrite a newer one";
    EXPECT_EQ(model.integrity().inventory_version_regressed, 1U);

    game::v1::WorldSnapshot s = Snapshot(1, 0);
    auto* item = s.add_spawned();
    item->set_entity_id(5);
    item->set_kind(game::v1::ENTITY_KIND_DROPPED_ITEM);
    model.OnWelcome(WelcomeFor(1));
    model.OnSnapshot(s);
    ASSERT_EQ(model.Visible().size(), 1U);
    EXPECT_EQ(model.Visible().front().kind, bot::SeenKind::kItem);
}

TEST(WorldModel, FlagsEarlySnapshotsGoingBackInTimeAndAForeignSelf) {
    WorldModel model;
    model.OnSnapshot(Snapshot(5, 0));  // before Welcome
    model.OnWelcome(WelcomeFor(1));
    model.OnSnapshot(Snapshot(6, 0));
    model.OnSnapshot(Snapshot(6, 0));  // the same tick again
    game::v1::WorldSnapshot foreign = Snapshot(8, 0);
    foreign.mutable_self()->set_entity_id(2);
    model.OnSnapshot(foreign);

    const WorldModel::Integrity& i = model.integrity();
    EXPECT_EQ(i.snapshot_before_welcome, 1U);
    EXPECT_EQ(i.server_tick_regressed, 1U);
    EXPECT_EQ(i.self_mismatch, 1U) << "only the snapshot that named someone else";
}

TEST(WorldModel, ASecondWelcomeIsANewZoneWithItsOwnPortalsTicksAndInputs) {
    WorldModel model;
    game::v1::Welcome meadow = WelcomeFor(1);
    meadow.set_zone_id(1);
    meadow.set_server_tick(500);
    auto* gate = meadow.add_portals();
    gate->set_portal_id(1);
    gate->mutable_position()->set_x(95.0F);
    gate->set_radius_m(3.0F);
    gate->set_to_zone_id(2);
    model.OnWelcome(meadow);
    ASSERT_EQ(model.portals().size(), 1U);
    EXPECT_EQ(model.portals().front().id, common::PortalId{1});
    EXPECT_EQ(model.portals().front().to_zone, common::ZoneId{2});
    game::v1::WorldSnapshot seen = Snapshot(510, 40);
    seen.add_spawned()->set_entity_id(2);
    model.OnSnapshot(seen);

    game::v1::Welcome highlands = WelcomeFor(7);
    highlands.set_zone_id(2);
    highlands.set_server_tick(30);
    model.OnWelcome(highlands);
    EXPECT_EQ(model.zone(), common::ZoneId{2});
    EXPECT_EQ(model.self(), common::EntityId{7});
    EXPECT_TRUE(model.portals().empty());
    EXPECT_EQ(model.visible_count(), 0U) << "what was seen belonged to the old zone";
    EXPECT_EQ(model.travel().zone_changes, 1U);

    game::v1::WorldSnapshot there = Snapshot(31, 41);
    there.mutable_self()->set_entity_id(7);
    there.add_spawned()->set_entity_id(2);  // another zone's entity 2
    model.OnSnapshot(there);
    EXPECT_EQ(model.integrity().Total(), 0U)
        << "a lower tick, a re-used entity id and a fresh input count are not contradictions";
}

TEST(WorldModel, CountsChangeZoneVerdicts) {
    WorldModel model;
    model.OnWelcome(WelcomeFor(1));
    for (std::uint64_t id = 1; id <= 3; ++id) {
        model.ExpectResult(common::RequestId{id}, RequestKind::kChangeZone);
    }
    const auto answer = [&](std::uint64_t id, game::v1::CommandStatus status) {
        game::v1::CommandResult result;
        result.set_request_id(id);
        result.set_status(status);
        model.OnCommandResult(result);
    };
    answer(1, game::v1::COMMAND_STATUS_PENDING);
    answer(1, game::v1::COMMAND_STATUS_ACCEPTED);
    answer(2, game::v1::COMMAND_STATUS_REJECTED);
    answer(3, game::v1::COMMAND_STATUS_UNKNOWN);
    EXPECT_EQ(model.travel().transfers_accepted, 1U);
    EXPECT_EQ(model.travel().transfers_rejected, 1U);
    EXPECT_EQ(model.travel().transfers_unknown, 1U);
}

}  // namespace
}  // namespace sa::loadgen
