#include "sa/zone/sim/zone.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace sa::zone::sim {
namespace {

using common::EntityId;
using common::RequestId;
using common::Vec2;

constexpr PortalId kEastGate{1};
constexpr Vec2 kGate{95.0F, 50.0F};
constexpr Vec2 kArrival{5.0F, 50.0F};
constexpr common::ZoneId kElsewhere{2};

MapData FieldWithPortal(common::ZoneId to = kElsewhere) {
    MapData map = test::OpenField();
    map.portals.push_back(Portal{
        .id = kEastGate, .position = kGate, .radius_m = 3.0F, .to_zone = to, .arrival = kArrival});
    return map;
}

std::vector<CommandResultEvent> Results(Zone& zone, EntityId player) {
    std::vector<CommandResultEvent> out;
    for (const DeliveredEvent& e : zone.DrainEvents()) {
        if (const auto* r = std::get_if<CommandResultEvent>(&e.event);
            r != nullptr && e.recipient == player) {
            out.push_back(*r);
        }
    }
    return out;
}

CommandResultEvent OnlyResult(Zone& zone, EntityId player) {
    const auto results = Results(zone, player);
    if (results.size() != 1) {
        throw std::logic_error{"expected exactly one command result"};
    }
    return results.front();
}

CommandResultEvent ChangeZoneAndTick(Zone& zone, EntityId player, std::uint64_t request,
                                     PortalId portal = kEastGate) {
    zone.Submit(
        ChangeZoneCommand{.player = player, .request_id = RequestId{request}, .portal = portal});
    zone.Tick();
    return OnlyResult(zone, player);
}

ZoneTransferRequest OnlyTransfer(Zone& zone) {
    const auto requests = zone.DrainHostRequests();
    if (requests.size() != 1 || !std::holds_alternative<ZoneTransferRequest>(requests.front())) {
        throw std::logic_error{"expected exactly one transfer request"};
    }
    return std::get<ZoneTransferRequest>(requests.front());
}

TEST(ZoneTransfer, DepartureIsValidatedAndAskedOfTheHostOnce) {
    Zone zone = test::MakeZone(FieldWithPortal());
    const EntityId far = test::Spawn(zone, 1, {50.0F, 50.0F});
    const EntityId near = test::Spawn(zone, 2, kGate + Vec2{1.0F, 0.0F});

    EXPECT_EQ(ChangeZoneAndTick(zone, far, 1).reason, RejectReason::kOutOfPortalRange);
    EXPECT_EQ(ChangeZoneAndTick(zone, near, 1, PortalId{9}).reason, RejectReason::kUnknownPortal);
    EXPECT_EQ(ChangeZoneAndTick(zone, near, 0).reason, RejectReason::kInvalidRequest);
    EXPECT_EQ(ChangeZoneAndTick(zone, near, 2).status, CommandStatus::kPending);
    EXPECT_EQ(ChangeZoneAndTick(zone, near, 3).reason, RejectReason::kTransferInProgress);
    EXPECT_EQ(ChangeZoneAndTick(zone, near, 2).status, CommandStatus::kPending)
        << "a retry of the waiting request answers pending again";

    const ZoneTransferRequest transfer = OnlyTransfer(zone);
    EXPECT_EQ(transfer.player, near);
    EXPECT_EQ(transfer.character, common::CharacterId{2});
    EXPECT_EQ(transfer.request_id, RequestId{2});
    EXPECT_EQ(transfer.to_zone, kElsewhere);
    EXPECT_EQ(transfer.arrival, kArrival);
    EXPECT_EQ(zone.stats().transfers_requested, 1U);
}

TEST(ZoneTransfer, ADepartingPlayerIsFrozen) {
    Zone zone = test::MakeZone(FieldWithPortal());
    const EntityId p = test::Spawn(zone, 1, kGate);
    zone.Submit(MoveCommand{.player = p, .input_seq = 1, .direction = {-1.0F, 0.0F}});
    zone.Tick();
    ASSERT_NE(test::PosOf(zone, p), kGate) << "walking before the request";
    ASSERT_EQ(ChangeZoneAndTick(zone, p, 1).status, CommandStatus::kPending);
    EXPECT_EQ(test::SnapshotOf(zone, p).velocity, Vec2{}) << "observers see it stop";

    const Vec2 before = test::PosOf(zone, p);
    zone.Submit(MoveCommand{.player = p, .input_seq = 2, .direction = {-1.0F, 0.0F}});
    test::TickN(zone, 5);
    EXPECT_EQ(test::PosOf(zone, p), before);
    zone.Submit(CastCommand{.player = p,
                            .request_id = RequestId{10},
                            .skill = test::kArrow,
                            .target = Vec2{-1.0F, 0.0F}});
    zone.Tick();
    EXPECT_EQ(OnlyResult(zone, p).reason, RejectReason::kTransferInProgress);
}

TEST(ZoneTransfer, CompletionRemovesThePlayerAndHandsTheResultToTheHost) {
    Zone zone = test::MakeZone(FieldWithPortal());
    const EntityId watcher = test::Spawn(zone, 1, kGate + Vec2{-5.0F, 0.0F});
    const EntityId traveller = test::Spawn(zone, 2, kGate);
    zone.Tick();
    (void)zone.CollectView(watcher);
    ASSERT_EQ(ChangeZoneAndTick(zone, traveller, 1).status, CommandStatus::kPending);
    const ZoneTransferRequest transfer = OnlyTransfer(zone);

    const auto result = zone.CompleteTransfer(transfer);

    ASSERT_TRUE(result.has_value());
    const CommandResultEvent completed = result.value_or(CommandResultEvent{});
    EXPECT_TRUE(completed.accepted());
    EXPECT_EQ(completed.request_id, RequestId{1});
    EXPECT_FALSE(zone.PositionOf(traveller).has_value());
    EXPECT_EQ(zone.stats().players, 1U);
    EXPECT_EQ(zone.stats().transfers_completed, 1U);
    zone.Tick();
    EXPECT_EQ(zone.CollectView(watcher).despawned, std::vector{traveller})
        << "observers see the traveller leave";
    EXPECT_FALSE(zone.CompleteTransfer(transfer).has_value()) << "gone already";
}

TEST(ZoneTransfer, ARefusalReleasesThePlayerAndIsRemembered) {
    Zone zone = test::MakeZone(FieldWithPortal());
    const EntityId p = test::Spawn(zone, 1, kGate);
    ASSERT_EQ(ChangeZoneAndTick(zone, p, 1).status, CommandStatus::kPending);
    const ZoneTransferRequest transfer = OnlyTransfer(zone);

    zone.FailTransfer(transfer, TransferOutcome::kZoneUnavailable);
    EXPECT_EQ(OnlyResult(zone, p).reason, RejectReason::kZoneUnavailable);
    EXPECT_EQ(ChangeZoneAndTick(zone, p, 1).reason, RejectReason::kZoneUnavailable)
        << "the same request answers the same way";
    EXPECT_TRUE(zone.DrainHostRequests().empty());

    zone.Submit(MoveCommand{.player = p, .input_seq = 1, .direction = {-1.0F, 0.0F}});
    zone.Tick();
    EXPECT_LT(test::PosOf(zone, p).x, kGate.x) << "free to move again";
}

TEST(ZoneTransfer, AnUnknownOutcomeReleasesThePlayerAndTheSameRequestRunsAgain) {
    Zone zone = test::MakeZone(FieldWithPortal());
    const EntityId p = test::Spawn(zone, 1, kGate);
    ASSERT_EQ(ChangeZoneAndTick(zone, p, 1).status, CommandStatus::kPending);
    const ZoneTransferRequest first = OnlyTransfer(zone);

    zone.FailTransfer(first, TransferOutcome::kUnavailable);
    EXPECT_EQ(OnlyResult(zone, p).status, CommandStatus::kUnknown);
    EXPECT_EQ(ChangeZoneAndTick(zone, p, 1).status, CommandStatus::kPending);
    EXPECT_EQ(OnlyTransfer(zone).request_id, RequestId{1});
}

TEST(ZoneTransfer, AnswersForAnotherRequestOrAFinishedDepartureChangeNothing) {
    Zone zone = test::MakeZone(FieldWithPortal());
    const EntityId p = test::Spawn(zone, 1, kGate);
    ASSERT_EQ(ChangeZoneAndTick(zone, p, 1).status, CommandStatus::kPending);
    const ZoneTransferRequest first = OnlyTransfer(zone);
    zone.FailTransfer(first, TransferOutcome::kUnavailable);
    (void)zone.DrainEvents();
    ASSERT_EQ(ChangeZoneAndTick(zone, p, 2).status, CommandStatus::kPending);
    (void)zone.DrainHostRequests();

    zone.FailTransfer(first, TransferOutcome::kZoneUnavailable);
    EXPECT_FALSE(zone.CompleteTransfer(first).has_value());
    EXPECT_TRUE(Results(zone, p).empty()) << "a late answer to the earlier request is ignored";
    EXPECT_EQ(ChangeZoneAndTick(zone, p, 2).status, CommandStatus::kPending) << "still departing";
}

TEST(ZoneTransfer, APortalBackIntoItsOwnZoneIsRefusedAtCreation) {
    const auto zone = Zone::Create(test::Config(), FieldWithPortal(test::Config().zone_id),
                                   test::AllRules(), test::Content(), Rng{1});
    ASSERT_FALSE(zone.has_value());
    EXPECT_NE(zone.error().find("leads back"), std::string::npos) << zone.error();
}

TEST(ZoneTransfer, AnArrivalWhereTheBodyDoesNotFitFallsBackToASpawnPoint) {
    Zone zone = test::MakeZone(test::FieldWithWall());
    const Vec2 inside_wall{51.0F, 30.0F};
    const auto id = zone.SpawnPlayer(common::CharacterId{1}, test::kWarrior, inside_wall);
    ASSERT_TRUE(id.has_value());
    const Vec2 at = test::PosOf(zone, *id);
    const auto& spawns = zone.map().spawn_points;
    EXPECT_NE(std::ranges::find(spawns, at), spawns.end());
    EXPECT_EQ(zone.stats().spawns_relocated, 1U);
}

}  // namespace
}  // namespace sa::zone::sim
