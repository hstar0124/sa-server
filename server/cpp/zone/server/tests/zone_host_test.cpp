#include "zone_host_test_support.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace sa::zone {
namespace {

using internal::v1::ZoneToGateway;
using namespace host_fixture;  // NOLINT(google-build-using-namespace): test fixtures only

TEST(ZoneHost, AttachSpawnsAPlayerAndAcknowledgesTheSession) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;

    host.Attach(sink, AttachFor("s1", 42));

    EXPECT_EQ(host.session_count(), 1U);
    EXPECT_EQ(zone.stats().players, 1U);
    EXPECT_EQ(Only(sink, ZoneToGateway::kSessionAttached).session_attached().gateway_session_id(),
              SessionWire("s1"));
}

TEST(ZoneHost, AttachWelcomesTheClientWithItsOwnEntityId) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;

    host.Attach(sink, AttachFor("s1", 42));

    const std::uint64_t entity =
        Only(sink, ZoneToGateway::kSessionAttached).session_attached().entity_id();
    const auto& welcome =
        Only(sink, ZoneToGateway::kSessionOutput).session_output().message().welcome();
    EXPECT_EQ(welcome.character_id(), 42U);
    EXPECT_EQ(welcome.entity_id(), entity);
}

TEST(ZoneHost, RefusesDuplicatesFullZoneAndWhenNotAuthoritative) {
    sim::Zone zone = MakeZone();
    bool authoritative = true;
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(1), economy, directory, [&] { return authoritative; }};
    FakeSink sink;

    host.Attach(sink, AttachFor("s1", 1));
    host.Attach(sink, AttachFor("s1", 1));
    host.Attach(sink, AttachFor("s2", 2));
    authoritative = false;
    host.Attach(sink, AttachFor("s3", 3));

    const auto rejected = sink.OfCase(ZoneToGateway::kSessionRejected);
    ASSERT_EQ(rejected.size(), 3U);
    EXPECT_EQ(rejected[0].session_rejected().reason(),
              internal::v1::REJECT_REASON_DUPLICATE_SESSION);
    EXPECT_EQ(rejected[1].session_rejected().reason(), internal::v1::REJECT_REASON_ZONE_FULL);
    EXPECT_EQ(rejected[2].session_rejected().reason(),
              internal::v1::REJECT_REASON_NOT_AUTHORITATIVE);
    EXPECT_EQ(host.session_count(), 1U);
}

TEST(ZoneHost, RefusesACharacterWhoseClassIsNotInTheContent) {
    constexpr std::uint32_t kClassNotInContent = 999;
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;

    auto attach = AttachFor("s1", 1);
    attach.set_class_id(kClassNotInContent);
    host.Attach(sink, attach);

    EXPECT_EQ(Only(sink, ZoneToGateway::kSessionRejected).session_rejected().reason(),
              internal::v1::REJECT_REASON_UNKNOWN_CLASS);
    EXPECT_TRUE(sink.OfCase(ZoneToGateway::kSessionAttached).empty());
    EXPECT_EQ(host.session_count(), 0U);
    EXPECT_EQ(zone.stats().players, 0U);
}

TEST(ZoneHost, InputReachesTheSimulationAndSnapshotsFollowCadence) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;
    host.Attach(sink, AttachFor("s1", 1));
    sink.sent.clear();

    internal::v1::SessionInput input;
    input.set_gateway_session_id(SessionWire("s1"));
    input.mutable_message()->mutable_move_input()->set_input_seq(1);
    input.mutable_message()->mutable_move_input()->mutable_direction()->set_x(1.0F);
    host.Input(input);
    zone.Tick();
    host.OnTick(zone.current_tick());  // tick 1: no snapshot (cadence 2)
    zone.Tick();
    host.OnTick(zone.current_tick());  // tick 2: snapshot

    const auto outputs = sink.OfCase(ZoneToGateway::kSessionOutput);
    ASSERT_EQ(outputs.size(), 1U);
    const auto& snapshot = outputs[0].session_output().message().world_snapshot();
    EXPECT_EQ(snapshot.server_tick(), 2U);
    EXPECT_EQ(snapshot.last_processed_input_seq(), 1U);
    EXPECT_GT(snapshot.self().position().x(), 50.0F) << "moved right for two ticks";
    EXPECT_EQ(host.stats().inputs, 1U);
}

// First snapshot delivered to a sink; a missing snapshot is a test bug.
const game::v1::WorldSnapshot& FirstSnapshotTo(const FakeSink& sink) {
    for (const auto& m : sink.sent) {
        if (m.body_case() == ZoneToGateway::kSessionOutput &&
            m.session_output().message().has_world_snapshot()) {
            return m.session_output().message().world_snapshot();
        }
    }
    throw std::logic_error{"no snapshot was sent"};
}

void TickAndSnapshot(sim::Zone& zone, ZoneHost& host) {
    zone.Tick();
    zone.Tick();
    host.OnTick(zone.current_tick());
}

TEST(ZoneHost, SessionsOnDifferentGatewaysSeeEachOther) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink gateway_a;
    FakeSink gateway_b;
    host.Attach(gateway_a, AttachFor("a", 1));
    host.Attach(gateway_b, AttachFor("b", 2));
    gateway_a.sent.clear();

    TickAndSnapshot(zone, host);

    const game::v1::WorldSnapshot& first = FirstSnapshotTo(gateway_a);
    EXPECT_EQ(first.spawned_size(), 2) << "a sees b and the rat";
    EXPECT_TRUE(std::ranges::any_of(first.spawned(), [](const game::v1::EntitySpawn& e) {
        return e.kind() == game::v1::ENTITY_KIND_PLAYER;
    })) << "a sees b";
}

TEST(ZoneHost, DetachDespawnsAndObserversAreTold) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink gateway_a;
    FakeSink gateway_b;
    host.Attach(gateway_a, AttachFor("a", 1));
    host.Attach(gateway_b, AttachFor("b", 2));
    TickAndSnapshot(zone, host);

    internal::v1::SessionDetach detach;
    detach.set_gateway_session_id(SessionWire("b"));
    host.Detach(detach);
    gateway_a.sent.clear();
    TickAndSnapshot(zone, host);

    EXPECT_EQ(FirstSnapshotTo(gateway_a).despawned_size(), 1) << "a is told b left";
    EXPECT_EQ(zone.stats().players, 1U);
    EXPECT_EQ(host.stats().detaches, 1U);
}

TEST(ZoneHost, AnAttachWhoseSessionIdIsNotAnIdIsCountedAndIgnored) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;
    internal::v1::SessionAttach attach = AttachFor("s1", 1);
    attach.set_gateway_session_id("short");  // ids are 16 bytes on the wire
    host.Attach(sink, attach);

    EXPECT_EQ(host.session_count(), 0U);
    EXPECT_EQ(host.stats().bad_inputs, 1U);
    EXPECT_TRUE(sink.sent.empty());
}

TEST(ZoneHost, ANonFiniteInputIsCountedAndNeverSubmitted) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;
    host.Attach(sink, AttachFor("s1", 1));

    internal::v1::SessionInput input;
    input.set_gateway_session_id(SessionWire("s1"));
    input.mutable_message()->mutable_move_input()->set_input_seq(1);
    input.mutable_message()->mutable_move_input()->mutable_direction()->set_x(
        std::numeric_limits<float>::infinity());
    host.Input(input);
    zone.Tick();

    EXPECT_EQ(host.stats().bad_inputs, 1U);
    EXPECT_EQ(host.stats().inputs, 0U);
    EXPECT_EQ(zone.stats().commands_applied, 0U);
}

TEST(ZoneHost, CastSkillYieldsACommandResultOnTheNextTick) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;
    host.Attach(sink, AttachFor("s1", 1));
    sink.sent.clear();

    internal::v1::SessionInput input;
    input.set_gateway_session_id(SessionWire("s1"));
    input.mutable_message()->mutable_cast_skill()->set_request_id(1);
    input.mutable_message()->mutable_cast_skill()->set_skill_id(1);
    input.mutable_message()->mutable_cast_skill()->set_target_entity_id(4242);  // no such monster
    host.Input(input);
    zone.Tick();
    host.OnTick(zone.current_tick());  // tick 1: events go out even without a snapshot

    const auto& result =
        Only(sink, ZoneToGateway::kSessionOutput).session_output().message().command_result();
    EXPECT_EQ(result.request_id(), 1U);
    EXPECT_EQ(result.status(), game::v1::COMMAND_STATUS_REJECTED);
    EXPECT_EQ(result.reject_code(), game::v1::REJECT_CODE_INVALID_TARGET);
    EXPECT_EQ(host.stats().events_sent, 1U);
}

TEST(ZoneHost, LinkClosureAndEvictionRemoveEveryoneOnThatLink) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink gateway_a;
    FakeSink gateway_b;
    host.Attach(gateway_a, AttachFor("a1", 1));
    host.Attach(gateway_a, AttachFor("a2", 2));
    host.Attach(gateway_b, AttachFor("b1", 3));

    host.LinkClosed(gateway_a);
    EXPECT_EQ(host.session_count(), 1U);
    EXPECT_EQ(zone.stats().players, 1U);

    host.EvictAll(internal::v1::EVICT_REASON_LEASE_LOST);
    EXPECT_EQ(host.session_count(), 0U);
    const auto evicted = gateway_b.OfCase(ZoneToGateway::kSessionEvicted);
    ASSERT_EQ(evicted.size(), 1U);
    EXPECT_EQ(evicted[0].session_evicted().reason(), internal::v1::EVICT_REASON_LEASE_LOST);
}

}  // namespace
}  // namespace sa::zone
