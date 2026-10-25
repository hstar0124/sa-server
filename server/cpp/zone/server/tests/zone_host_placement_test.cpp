#include "zone_host_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace sa::zone {
namespace {

using internal::v1::ZoneToGateway;
using namespace host_fixture;  // NOLINT(google-build-using-namespace): test fixtures only

internal::v1::SessionAttach AttachAt(const std::string& session, std::uint64_t character,
                                     common::Vec2 at, std::uint64_t epoch = 1) {
    internal::v1::SessionAttach a = AttachFor(session, character);
    a.set_owner_epoch(epoch);
    a.mutable_position()->set_x(at.x);
    a.mutable_position()->set_y(at.y);
    return a;
}

internal::v1::SessionInput ChangeZoneInput(const std::string& session, std::uint64_t request) {
    internal::v1::SessionInput input;
    input.set_gateway_session_id(SessionWire(session));
    input.mutable_message()->mutable_change_zone()->set_request_id(request);
    input.mutable_message()->mutable_change_zone()->set_portal_id(kGate.value());
    return input;
}

internal::v1::SessionInput MoveInput(const std::string& session, std::uint32_t seq,
                                     common::Vec2 direction) {
    internal::v1::SessionInput input;
    input.set_gateway_session_id(SessionWire(session));
    auto* move = input.mutable_message()->mutable_move_input();
    move->set_input_seq(seq);
    move->mutable_direction()->set_x(direction.x);
    move->mutable_direction()->set_y(direction.y);
    return input;
}

void Step(sim::Zone& zone, ZoneHost& host, int ticks = 1) {
    for (int i = 0; i < ticks; ++i) {
        zone.Tick();
        host.OnTick(zone.current_tick());
    }
}

backend::v1::TransferCharacterResponse Moved(std::uint64_t epoch,
                                             std::uint32_t zone = kDestination.value()) {
    backend::v1::TransferCharacterResponse moved;
    moved.set_outcome(backend::v1::TRANSFER_OUTCOME_TRANSFERRED);
    moved.mutable_to_zone()->set_zone_id(zone);
    moved.mutable_to_zone()->set_zone_instance_id(common::InstanceId::Generate().ToWire());
    moved.set_owner_epoch(epoch);
    return moved;
}

backend::v1::TransferCharacterResponse Refused(backend::v1::TransferOutcome outcome) {
    backend::v1::TransferCharacterResponse refused;
    refused.set_outcome(outcome);
    return refused;
}

backend::v1::SaveCheckpointResponse CheckpointAnswer(backend::v1::CheckpointOutcome outcome) {
    backend::v1::SaveCheckpointResponse response;
    response.set_outcome(outcome);
    return response;
}

class ZoneHostPlacement : public ::testing::Test {
protected:
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    FakeTrades trades;
    ZoneHost::Identity identity = Identity();
    ZoneHost host{zone, identity, economy, directory, trades, [] { return true; }};
    FakeSink sink;

    // Session "s1" (character 42) stands in the portal and asked to leave; the departure is with
    // the Backend.
    void Depart() {
        host.Attach(sink, AttachAt("s1", 42, kGatePosition));
        host.Input(ChangeZoneInput("s1", 5));
        Step(zone, host);
        sink.sent.clear();
    }
};

TEST_F(ZoneHostPlacement, AttachWithAPositionSpawnsThereAndTheWelcomeListsThePortals) {
    host.Attach(sink, AttachAt("s1", 42, {12.0F, 34.0F}));

    const common::EntityId entity{
        Only(sink, ZoneToGateway::kSessionAttached).session_attached().entity_id()};
    EXPECT_EQ(zone.PositionOf(entity), (common::Vec2{12.0F, 34.0F}));
    const auto welcome =
        Single(OutputsWhere(sink, [](const game::v1::ServerMessage& m) { return m.has_welcome(); }),
               "welcome");
    ASSERT_EQ(welcome.welcome().portals_size(), 1);
    EXPECT_EQ(welcome.welcome().portals(0).portal_id(), kGate.value());
    EXPECT_EQ(welcome.welcome().portals(0).to_zone_id(), kDestination.value());
}

TEST_F(ZoneHostPlacement, AnAttachUnderAnOlderOrEqualEpochForTheSameCharacterIsStale) {
    host.Attach(sink, AttachAt("s1", 42, kSpawn, 3));
    host.Attach(sink, AttachAt("s2", 42, kSpawn, 3));
    host.Attach(sink, AttachAt("s3", 42, kSpawn, 2));

    const auto rejected = sink.OfCase(ZoneToGateway::kSessionRejected);
    EXPECT_EQ(rejected.size(), 2U);
    EXPECT_TRUE(std::ranges::all_of(rejected, [](const ZoneToGateway& m) {
        return m.session_rejected().reason() == internal::v1::REJECT_REASON_STALE_EPOCH;
    }));
    EXPECT_EQ(host.stats().stale_attaches, 2U);
    EXPECT_EQ(host.session_count(), 1U);
}

TEST_F(ZoneHostPlacement, ANewerEpochForALiveCharacterEndsTheOlderSession) {
    FakeSink old_gateway;
    host.Attach(old_gateway, AttachAt("s1", 42, kSpawn, 1));
    host.Attach(sink, AttachAt("s2", 42, kSpawn, 2));

    const auto& evicted = Only(old_gateway, ZoneToGateway::kSessionEvicted).session_evicted();
    EXPECT_EQ(evicted.gateway_session_id(), SessionWire("s1"));
    EXPECT_EQ(evicted.reason(), internal::v1::EVICT_REASON_NOT_OWNER);
    EXPECT_EQ(sink.OfCase(ZoneToGateway::kSessionAttached).size(), 1U);
    EXPECT_EQ(host.session_count(), 1U);
    EXPECT_EQ(zone.stats().players, 1U) << "one body per character";
    EXPECT_EQ(host.stats().superseded_sessions, 1U);
}

TEST_F(ZoneHostPlacement, CheckpointsFollowTheCadenceAndSkipAStandingCharacter) {
    host.Attach(sink, AttachFor("s1", 42));
    host.Input(MoveInput("s1", 1, {0.0F, 1.0F}));

    Step(zone, host, 4);  // tick 4: moved since attach
    const auto first = Single(directory.checkpoints, "first checkpoint");
    EXPECT_EQ(first.checkpoint_version(), 1U);
    EXPECT_EQ(first.character_id(), 42U);
    EXPECT_EQ(first.zone_id(), 1U);
    EXPECT_EQ(first.authority().zone_instance_id(), identity.zone_instance.ToWire());
    EXPECT_GT(first.position().y(), kSpawn.y);
    directory.checkpoint_replies.front()(CheckpointAnswer(backend::v1::CHECKPOINT_OUTCOME_SAVED));

    Step(zone, host, 2);  // still walking
    host.Input(MoveInput("s1", 2, {}));
    Step(zone, host, 2);  // tick 8: moved since tick 4, saved again
    ASSERT_EQ(directory.checkpoints.size(), 2U);
    EXPECT_EQ(directory.checkpoints[1].checkpoint_version(), 2U);
    directory.checkpoint_replies[1](CheckpointAnswer(backend::v1::CHECKPOINT_OUTCOME_SAVED));

    Step(zone, host, 4);  // tick 12: standing still
    EXPECT_EQ(directory.checkpoints.size(), 2U);
    EXPECT_EQ(host.stats().checkpoints_saved, 2U);
}

TEST_F(ZoneHostPlacement, AnUnansweredCheckpointIsSentAgainAtTheNextCadence) {
    host.Attach(sink, AttachFor("s1", 42));
    host.Input(MoveInput("s1", 1, {0.0F, 1.0F}));
    Step(zone, host, 1);
    host.Input(MoveInput("s1", 2, {}));
    Step(zone, host, 3);
    Single(directory.checkpoint_replies, "checkpoint")(std::nullopt);

    Step(zone, host, 4);
    ASSERT_EQ(directory.checkpoints.size(), 2U) << "same position, but the first save is unknown";
    EXPECT_EQ(directory.checkpoints[1].checkpoint_version(), 2U);
    EXPECT_EQ(directory.checkpoints[1].position().y(), directory.checkpoints[0].position().y());
}

TEST_F(ZoneHostPlacement, LeavingSavesWhereTheCharacterStood) {
    host.Attach(sink, AttachAt("s1", 42, {20.0F, 30.0F}));
    internal::v1::SessionDetach detach;
    detach.set_gateway_session_id(SessionWire("s1"));
    host.Detach(detach);

    const auto saved = Single(directory.checkpoints, "checkpoint on detach");
    EXPECT_FLOAT_EQ(saved.position().x(), 20.0F);
    EXPECT_FLOAT_EQ(saved.position().y(), 30.0F);

    host.Attach(sink, AttachAt("s2", 43, {21.0F, 31.0F}));
    host.LinkClosed(sink);
    EXPECT_EQ(directory.checkpoints.size(), 2U) << "a closed gateway link saves too";
}

TEST_F(ZoneHostPlacement, ACheckpointRefusedForAuthorityEndsTheSession) {
    host.Attach(sink, AttachFor("s1", 42));
    host.Input(MoveInput("s1", 1, {0.0F, 1.0F}));
    Step(zone, host, 4);
    Single(directory.checkpoint_replies,
           "checkpoint")(CheckpointAnswer(backend::v1::CHECKPOINT_OUTCOME_STALE_AUTHORITY));

    EXPECT_EQ(Only(sink, ZoneToGateway::kSessionEvicted).session_evicted().reason(),
              internal::v1::EVICT_REASON_NOT_OWNER);
    EXPECT_EQ(host.session_count(), 0U);
    EXPECT_EQ(host.stats().checkpoints_refused, 1U);
}

TEST_F(ZoneHostPlacement, ATransferHandsTheSessionToTheGatewayOnceTheBackendMovedTheCharacter) {
    host.Attach(sink, AttachAt("s1", 42, kGatePosition));
    sink.sent.clear();
    host.Input(ChangeZoneInput("s1", 5));
    Step(zone, host);

    const auto request = Single(directory.transfers, "transfer");
    EXPECT_EQ(request.to_zone_id(), kDestination.value());
    EXPECT_EQ(request.character_id(), 42U);
    EXPECT_EQ(request.authority().owner_epoch(), 1U);
    EXPECT_FLOAT_EQ(request.arrival_position().x(), kArrival.x);
    EXPECT_EQ(request.transfer_id().size(), common::TransferId::kBytes);
    EXPECT_EQ(Single(CommandResults(sink), "pending").status(), game::v1::COMMAND_STATUS_PENDING);

    Single(directory.transfer_replies, "transfer reply")(Moved(2));

    const auto results = CommandResults(sink);
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results.back().status(), game::v1::COMMAND_STATUS_ACCEPTED);
    const auto& handover = Only(sink, ZoneToGateway::kSessionTransfer).session_transfer();
    EXPECT_EQ(handover.gateway_session_id(), SessionWire("s1"));
    EXPECT_EQ(handover.to_zone_id(), kDestination.value());
    EXPECT_EQ(handover.owner_epoch(), 2U);
    EXPECT_FLOAT_EQ(handover.arrival_position().x(), kArrival.x);
    EXPECT_FLOAT_EQ(handover.arrival_position().y(), kArrival.y);
    EXPECT_TRUE(sink.sent.back().has_session_transfer()) << "the result goes out first";
    EXPECT_EQ(host.session_count(), 0U);
    EXPECT_EQ(zone.stats().players, 0U);
    EXPECT_EQ(host.stats().transfers_completed, 1U);
}

TEST_F(ZoneHostPlacement, AnUnansweredTransferIsRetriedUnderTheSameTransferId) {
    Depart();
    Single(directory.transfer_replies, "transfer reply")(std::nullopt);
    Step(zone, host);
    EXPECT_EQ(Single(CommandResults(sink), "result").status(), game::v1::COMMAND_STATUS_UNKNOWN);
    EXPECT_EQ(host.session_count(), 1U);

    host.Input(ChangeZoneInput("s1", 5));
    Step(zone, host);
    ASSERT_EQ(directory.transfers.size(), 2U);
    EXPECT_EQ(directory.transfers[1].transfer_id(), directory.transfers[0].transfer_id())
        << "the Backend answers the retry from the stored outcome";

    backend::v1::TransferCharacterResponse stored = Moved(2);
    stored.set_outcome(backend::v1::TRANSFER_OUTCOME_ALREADY_TRANSFERRED);
    directory.transfer_replies[1](stored);
    EXPECT_EQ(sink.OfCase(ZoneToGateway::kSessionTransfer).size(), 1U);
}

TEST_F(ZoneHostPlacement, ARefusedTransferKeepsThePlayerHere) {
    Depart();
    Single(directory.transfer_replies,
           "transfer reply")(Refused(backend::v1::TRANSFER_OUTCOME_NO_ZONE_AVAILABLE));
    Step(zone, host);

    const game::v1::CommandResult result = Single(CommandResults(sink), "result");
    EXPECT_EQ(result.status(), game::v1::COMMAND_STATUS_REJECTED);
    EXPECT_EQ(result.reject_code(), game::v1::REJECT_CODE_ZONE_UNAVAILABLE);
    EXPECT_EQ(host.session_count(), 1U);
    EXPECT_EQ(zone.stats().players, 1U);
    EXPECT_EQ(host.stats().transfers_refused, 1U);
}

TEST_F(ZoneHostPlacement, ATransferRefusedForAuthorityEndsTheSession) {
    Depart();
    Single(directory.transfer_replies,
           "transfer reply")(Refused(backend::v1::TRANSFER_OUTCOME_STALE_AUTHORITY));

    EXPECT_EQ(Only(sink, ZoneToGateway::kSessionEvicted).session_evicted().reason(),
              internal::v1::EVICT_REASON_NOT_OWNER);
    EXPECT_EQ(host.session_count(), 0U);
    EXPECT_EQ(zone.stats().players, 0U);
}

TEST_F(ZoneHostPlacement, ALateCheckpointRefusalLeavesADepartureToItsOwnAnswer) {
    // A save sent before the departure can reach the Backend after the transfer committed.
    host.Attach(sink, AttachAt("s1", 42, kGatePosition));
    host.Input(MoveInput("s1", 1, {-1.0F, 0.0F}));
    Step(zone, host);
    host.Input(MoveInput("s1", 2, {}));
    Step(zone, host, 3);
    const auto in_flight = Single(directory.checkpoint_replies, "checkpoint");
    host.Input(ChangeZoneInput("s1", 5));
    Step(zone, host);
    sink.sent.clear();

    in_flight(CheckpointAnswer(backend::v1::CHECKPOINT_OUTCOME_STALE_AUTHORITY));
    EXPECT_TRUE(sink.OfCase(ZoneToGateway::kSessionEvicted).empty());
    Step(zone, host, 3);
    EXPECT_EQ(directory.checkpoints.size(), 1U) << "no save while the departure is open";

    Single(directory.transfer_replies, "transfer reply")(Moved(2));
    EXPECT_EQ(sink.OfCase(ZoneToGateway::kSessionTransfer).size(), 1U);
}

TEST_F(ZoneHostPlacement, AReplyThatNamesAnotherZoneOrNoNewerEpochEndsTheSession) {
    Depart();
    Single(directory.transfer_replies, "transfer reply")(Moved(1));

    EXPECT_TRUE(sink.OfCase(ZoneToGateway::kSessionTransfer).empty());
    EXPECT_EQ(Only(sink, ZoneToGateway::kSessionEvicted).session_evicted().reason(),
              internal::v1::EVICT_REASON_NOT_OWNER);
    EXPECT_EQ(host.session_count(), 0U);
}

}  // namespace
}  // namespace sa::zone
