#include "sa/zone/sim/zone.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace sa::zone::sim {
namespace {

using common::EntityId;
using common::RequestId;
using common::Vec2;

constexpr Vec2 kBesideSlime{68.0F, 70.0F};
constexpr Vec2 kHome{10.0F, 10.0F};

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

CommandResultEvent OnlyResult(Zone& zone, EntityId player) {
    const auto results = ResultsFor(zone.DrainEvents(), player);
    if (results.size() != 1) {
        throw std::logic_error{"expected exactly one command result"};
    }
    return results.front();
}

// Strikes until the slime dies; the combat tests cover the mechanics, this only needs a corpse.
void KillTheSlime(Zone& zone, EntityId player) {
    std::uint64_t request = 100;
    while (!zone.LiveMonsters().empty()) {
        zone.Submit(CastCommand{.player = player,
                                .request_id = RequestId{request++},
                                .skill = test::kStrike,
                                .target = test::OnlyMonster(zone)});
        test::TickN(zone, 9);  // strike cooldown 800 ms at 10 Hz
        if (request > 200) {
            throw std::logic_error{"the slime refuses to die"};
        }
    }
    (void)zone.DrainEvents();
}

EntityId DropGelAt(Zone& zone, Vec2 at,
                   common::ItemInstanceId instance = common::ItemInstanceId::Generate()) {
    const GroundItem gel{.instance = instance, .item = test::kGel, .position = at};
    const auto ids = zone.SpawnGroundItems(std::span{&gel, 1});
    if (ids.size() != 1) {
        throw std::logic_error{"gel not spawned"};
    }
    return ids.front();
}

CommandResultEvent PickupAndTick(Zone& zone, EntityId player, std::uint64_t request,
                                 EntityId item) {
    zone.Submit(PickupCommand{.player = player, .request_id = RequestId{request}, .item = item});
    zone.Tick();
    return OnlyResult(zone, player);
}

// The one reservation the zone handed to the host since the last drain.
PickupReservation TakeReservation(Zone& zone) {
    const auto requests = zone.DrainHostRequests();
    if (requests.size() != 1 || !std::holds_alternative<PickupReservation>(requests.front())) {
        throw std::logic_error{"expected exactly one pickup reservation"};
    }
    return std::get<PickupReservation>(requests.front());
}

TEST(ZoneItems, AKillByAPlayerEmitsOneKillRecordWithTheRolledDrops) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId p = test::Spawn(zone, 1, kBesideSlime);

    KillTheSlime(zone, p);

    const auto requests = zone.DrainHostRequests();
    ASSERT_EQ(requests.size(), 1U);
    const auto* kill = std::get_if<KillRecord>(&requests.front());
    ASSERT_NE(kill, nullptr);
    EXPECT_EQ(kill->killer, p);
    EXPECT_EQ(kill->killer_character, common::CharacterId{1});
    EXPECT_EQ(kill->monster, test::kSlime);
    ASSERT_EQ(kill->rolls.size(), 1U) << "gel always drops, the sword never does";
    EXPECT_EQ(kill->rolls.front().item, test::kGel);
    EXPECT_LT(common::Distance(kill->rolls.front().position, Vec2{70.0F, 70.0F}), 1.0F);
    EXPECT_EQ(zone.stats().drop_rolls, 1U);
    EXPECT_TRUE(zone.GroundItems().empty())
        << "nothing lies on the ground until the Backend commits";
    EXPECT_TRUE(zone.DrainHostRequests().empty()) << "drained once";
}

TEST(ZoneItems, CommittedItemsBecomeVisibleEntitiesThatExpire) {
    Zone zone = test::MakeZone();
    const EntityId p = test::Spawn(zone, 1, kHome);
    zone.Tick();
    (void)zone.CollectView(p);

    const EntityId gel = DropGelAt(zone, kHome + Vec2{1.0F, 0.0F});
    zone.Tick();
    const PlayerView appeared = zone.CollectView(p);

    ASSERT_EQ(appeared.spawned.size(), 1U);
    EXPECT_EQ(appeared.spawned.front().id, gel);
    EXPECT_EQ(appeared.spawned.front().kind, EntityKind::kDroppedItem);
    EXPECT_EQ(appeared.spawned.front().template_id, test::kGel.value());
    EXPECT_EQ(zone.KindOf(gel), EntityKind::kDroppedItem);
    EXPECT_EQ(zone.stats().ground_items, 1U);

    test::TickN(zone, 30);  // despawn 3000 ms at 10 Hz
    const PlayerView vanished = zone.CollectView(p);
    EXPECT_TRUE(zone.GroundItems().empty());
    ASSERT_EQ(vanished.despawned.size(), 1U);
    EXPECT_EQ(vanished.despawned.front(), gel);
}

TEST(ZoneItems, PickupIsValidatedThenReservedAndSettledByTheBackendAnswer) {
    Zone zone = test::MakeZone();
    const EntityId p = test::Spawn(zone, 1, kHome);
    const EntityId other = test::Spawn(zone, 2, kHome);
    const EntityId near = DropGelAt(zone, kHome + Vec2{1.0F, 0.0F});
    const EntityId far = DropGelAt(zone, kHome + Vec2{5.0F, 0.0F});

    EXPECT_EQ(PickupAndTick(zone, p, 1, far).reason, RejectReason::kOutOfRange);
    EXPECT_EQ(PickupAndTick(zone, p, 2, EntityId{999}).reason, RejectReason::kItemNotFound);
    const CommandResultEvent pending = PickupAndTick(zone, p, 3, near);
    EXPECT_EQ(pending.status, CommandStatus::kPending);
    EXPECT_EQ(PickupAndTick(zone, other, 1, near).reason, RejectReason::kItemReserved);
    EXPECT_EQ(PickupAndTick(zone, p, 3, near).status, CommandStatus::kPending)
        << "a retry of the pending request answers pending again";

    const PickupReservation reservation = TakeReservation(zone);  // the retry did not reserve again
    EXPECT_EQ(reservation.player, p);
    EXPECT_EQ(reservation.character, common::CharacterId{1});
    EXPECT_EQ(reservation.request_id, RequestId{3});
    EXPECT_EQ(reservation.item, near);
    EXPECT_FALSE(reservation.instance.IsNil());
    EXPECT_EQ(zone.stats().pickups_requested, 1U);

    zone.ResolvePickup(reservation, PickupOutcome::kAcquired);
    const CommandResultEvent settled = OnlyResult(zone, p);
    EXPECT_TRUE(settled.accepted());
    EXPECT_EQ(settled.request_id, RequestId{3});
    EXPECT_EQ(zone.GroundItems(), std::vector{far});
    EXPECT_EQ(zone.stats().pickups_acquired, 1U);
    EXPECT_TRUE(PickupAndTick(zone, p, 3, near).accepted())
        << "a late retry gets the final verdict without a new reservation";
    EXPECT_TRUE(zone.DrainHostRequests().empty());
}

TEST(ZoneItems, RefusalsReleaseOrRemoveTheItemAccordingToTheOutcome) {
    Zone zone = test::MakeZone();
    const EntityId p = test::Spawn(zone, 1, kHome);
    const EntityId gel = DropGelAt(zone, kHome + Vec2{1.0F, 0.0F});

    ASSERT_EQ(PickupAndTick(zone, p, 1, gel).status, CommandStatus::kPending);
    zone.ResolvePickup(TakeReservation(zone), PickupOutcome::kInventoryFull);
    EXPECT_EQ(OnlyResult(zone, p).reason, RejectReason::kInventoryFull);
    EXPECT_EQ(zone.GroundItems(), std::vector{gel}) << "a full inventory leaves the item lying";

    ASSERT_EQ(PickupAndTick(zone, p, 2, gel).status, CommandStatus::kPending)
        << "released, so a new request can reserve it";
    zone.ResolvePickup(TakeReservation(zone), PickupOutcome::kUnavailable);
    const CommandResultEvent unknown = OnlyResult(zone, p);
    EXPECT_EQ(unknown.status, CommandStatus::kUnknown);
    EXPECT_EQ(unknown.reason, RejectReason::kServiceUnavailable);
    EXPECT_EQ(PickupAndTick(zone, p, 2, gel).status, CommandStatus::kPending)
        << "the same request id runs again after an unknown outcome";
    const PickupReservation again = TakeReservation(zone);

    zone.ResolvePickup(again, PickupOutcome::kAlreadyTaken);
    EXPECT_EQ(OnlyResult(zone, p).reason, RejectReason::kItemNotFound)
        << "taken by someone else is not a reservation that might clear";
    EXPECT_TRUE(zone.GroundItems().empty()) << "taken by someone else: gone from the ground";
}

TEST(ZoneItems, ReservedItemsDoNotExpireAndTheDeadCannotReach) {
    Zone zone = test::MakeZone(test::FieldWithSlime());
    const EntityId p = test::Spawn(zone, 1, kBesideSlime, test::kPauper);
    const EntityId gel = DropGelAt(zone, kBesideSlime + Vec2{0.5F, 0.0F});

    ASSERT_EQ(PickupAndTick(zone, p, 1, gel).status, CommandStatus::kPending);
    test::TickN(zone, 40);
    EXPECT_EQ(zone.GroundItems(), std::vector{gel}) << "held for the pending answer";
    zone.ResolvePickup(TakeReservation(zone), PickupOutcome::kInventoryFull);
    (void)zone.DrainEvents();

    // The slime beats the pauper to death while the gel lies at their feet.
    for (int i = 0; i < 400 && test::VitalsOf(zone, p).IsAlive(); ++i) {
        zone.Tick();
    }
    ASSERT_FALSE(test::VitalsOf(zone, p).IsAlive());
    (void)zone.DrainEvents();
    const EntityId fresh = DropGelAt(zone, test::PosOf(zone, p));
    EXPECT_EQ(PickupAndTick(zone, p, 2, fresh).reason, RejectReason::kDead);
}

TEST(ZoneItems, RequestsWithoutAnIdAreRejectedAndNeverRemembered) {
    Zone zone = test::MakeZone();
    const EntityId p = test::Spawn(zone, 1, kHome);
    const EntityId gel = DropGelAt(zone, kHome + Vec2{1.0F, 0.0F});

    const CommandResultEvent pickup = PickupAndTick(zone, p, 0, gel);
    EXPECT_EQ(pickup.reason, RejectReason::kInvalidRequest);
    EXPECT_TRUE(zone.DrainHostRequests().empty()) << "nothing was reserved";

    zone.Submit(
        CastCommand{.player = p, .request_id = RequestId{}, .skill = test::kStrike, .target = gel});
    zone.Tick();
    EXPECT_EQ(OnlyResult(zone, p).reason, RejectReason::kInvalidRequest);
    EXPECT_EQ(PickupAndTick(zone, p, 0, gel).reason, RejectReason::kInvalidRequest)
        << "id 0 never matches a remembered result";
}

TEST(ZoneItems, APendingPickupOutlivesARetryWindowFullOfOtherRequests) {
    Zone zone = test::MakeZone();
    const EntityId p = test::Spawn(zone, 1, kHome);
    const EntityId gel = DropGelAt(zone, kHome + Vec2{1.0F, 0.0F});
    ASSERT_EQ(PickupAndTick(zone, p, 1, gel).status, CommandStatus::kPending);
    const PickupReservation reservation = TakeReservation(zone);

    // More decided requests than the window holds; each is a cast of a skill nobody knows.
    for (std::uint64_t r = 2; r < 2 + test::kTestRetryWindow + 1; ++r) {
        zone.Submit(CastCommand{
            .player = p, .request_id = RequestId{r}, .skill = SkillId{999}, .target = gel});
    }
    zone.Tick();
    (void)zone.DrainEvents();

    EXPECT_EQ(PickupAndTick(zone, p, 1, gel).status, CommandStatus::kPending)
        << "answered from the reservation, not from the window";
    EXPECT_TRUE(zone.DrainHostRequests().empty()) << "the retry did not reserve a second time";
    zone.ResolvePickup(reservation, PickupOutcome::kAcquired);
    EXPECT_TRUE(OnlyResult(zone, p).accepted()) << "one final answer for the one request";
}

TEST(ZoneItems, NotOwnerAndRequestMismatchAreFinalAndReleaseTheItem) {
    Zone zone = test::MakeZone();
    const EntityId p = test::Spawn(zone, 1, kHome);
    const EntityId gel = DropGelAt(zone, kHome + Vec2{1.0F, 0.0F});

    ASSERT_EQ(PickupAndTick(zone, p, 1, gel).status, CommandStatus::kPending);
    zone.ResolvePickup(TakeReservation(zone), PickupOutcome::kNotOwner);
    EXPECT_EQ(OnlyResult(zone, p).reason, RejectReason::kNotOwner);
    EXPECT_EQ(PickupAndTick(zone, p, 1, gel).reason, RejectReason::kNotOwner)
        << "final: a retry gets the same verdict and does not reach the Backend";
    EXPECT_TRUE(zone.DrainHostRequests().empty());

    ASSERT_EQ(PickupAndTick(zone, p, 2, gel).status, CommandStatus::kPending)
        << "the item was released";
    zone.ResolvePickup(TakeReservation(zone), PickupOutcome::kRequestMismatch);
    EXPECT_EQ(OnlyResult(zone, p).reason, RejectReason::kInvalidRequest);
    EXPECT_EQ(zone.GroundItems(), std::vector{gel});
}

TEST(ZoneItems, AnAnswerReachesThePlayerEvenWhenTheItemIsGone) {
    Zone zone = test::MakeZone();
    const EntityId p = test::Spawn(zone, 1, kHome);
    zone.ResolvePickup(PickupReservation{.player = p,
                                         .character = common::CharacterId{1},
                                         .request_id = RequestId{7},
                                         .item = EntityId{999},
                                         .instance = common::ItemInstanceId::Generate()},
                       PickupOutcome::kAcquired);
    const CommandResultEvent result = OnlyResult(zone, p);
    EXPECT_TRUE(result.accepted());
    EXPECT_EQ(result.request_id, RequestId{7});
}

TEST(ZoneItems, ItemsTheBackendAlreadyHoldsLeaveTheGroundUnlessReserved) {
    Zone zone = test::MakeZone();
    const EntityId p = test::Spawn(zone, 1, kHome);
    const auto lost = common::ItemInstanceId::Generate();
    const auto reserved = common::ItemInstanceId::Generate();
    const auto unrelated = common::ItemInstanceId::Generate();
    const EntityId lost_item = DropGelAt(zone, kHome + Vec2{1.0F, 0.0F}, lost);
    const EntityId reserved_item = DropGelAt(zone, kHome + Vec2{0.0F, 1.0F}, reserved);
    const EntityId unrelated_item = DropGelAt(zone, kHome + Vec2{-1.0F, 0.0F}, unrelated);
    ASSERT_EQ(PickupAndTick(zone, p, 1, reserved_item).status, CommandStatus::kPending);

    const std::vector held{lost, reserved};
    zone.RemoveSettledGroundItems(held);

    auto ground = zone.GroundItems();
    std::ranges::sort(ground);
    auto expected = std::vector{reserved_item, unrelated_item};
    std::ranges::sort(expected);
    EXPECT_EQ(ground, expected) << "the lost pickup's item left; the reserved one waits";
    EXPECT_EQ(std::ranges::count(ground, lost_item), 0);
}

}  // namespace
}  // namespace sa::zone::sim
