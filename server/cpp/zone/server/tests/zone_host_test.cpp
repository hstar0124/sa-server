#include "zone_host.hpp"

#include "sa/common/instance_id.hpp"
#include "sa/zone/sim/zone.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <vector>

namespace sa::zone {
namespace {

using internal::v1::ZoneToGateway;

class FakeSink final : public LinkSink {
public:
    void Send(const ZoneToGateway& message) override { sent.push_back(message); }

    [[nodiscard]] std::vector<ZoneToGateway> OfCase(ZoneToGateway::BodyCase c) const {
        std::vector<ZoneToGateway> out;
        for (const auto& m : sent) {
            if (m.body_case() == c) {
                out.push_back(m);
            }
        }
        return out;
    }

    std::vector<ZoneToGateway> sent;
};

sim::ContentBundle TestContent() {
    sim::ContentBundle c;
    c.version = "test";
    c.classes[sim::ClassId{1}] =
        sim::ClassDefinition{.id = sim::ClassId{1}, .name = "w", .max_hp = 100, .max_mp = 10};
    return c;
}

sim::Zone MakeZone() {
    auto zone = sim::Zone::Create(
        sim::ZoneConfig{.world_id = common::WorldId{1},
                        .zone_id = common::ZoneId{1},
                        .simulation_rate = sim::TickRate{.hz = 10}},
        sim::MapData{.size = {100.0F, 100.0F},
                     .cell_size = 10.0F,
                     .obstacles = {},
                     .spawn_points = {{50.0F, 50.0F}}},
        sim::MovementRules{.max_speed_mps = 10.0F,
                           .aoi_enter_radius_m = 20.0F,
                           .aoi_leave_radius_m = 25.0F,
                           .commands_per_tick_budget = 100},
        sim::CombatRules{.monster_think_hz = 5, .player_respawn = std::chrono::seconds{1}},
        TestContent(), sim::Rng{1});
    if (!zone) {
        throw std::logic_error{zone.error()};
    }
    return std::move(*zone);
}

ZoneHost::Identity Identity(std::uint32_t max_sessions = 10) {
    return ZoneHost::Identity{.zone_instance = common::InstanceId::Generate(),
                              .simulation_hz = 10,
                              .ticks_per_snapshot = 2,
                              .max_sessions = max_sessions};
}

internal::v1::SessionAttach AttachFor(const std::string& session, std::uint64_t character) {
    internal::v1::SessionAttach a;
    a.set_gateway_session_id(session);
    a.set_character_id(character);
    a.set_class_id(1);
    a.set_owner_epoch(1);
    return a;
}

// The one message of the given case sent to the sink; anything else is a test bug.
const ZoneToGateway& Only(const FakeSink& sink, ZoneToGateway::BodyCase c) {
    const auto matches = sink.OfCase(c);
    if (matches.size() != 1) {
        throw std::logic_error{"expected exactly one message of that case"};
    }
    static ZoneToGateway copy;
    copy = matches.front();
    return copy;
}

TEST(ZoneHost, AttachSpawnsAPlayerAndAcknowledgesTheSession) {
    sim::Zone zone = MakeZone();
    ZoneHost host{zone, Identity(), [] { return true; }};
    FakeSink sink;

    host.Attach(sink, AttachFor("s1", 42));

    EXPECT_EQ(host.session_count(), 1U);
    EXPECT_EQ(zone.stats().players, 1U);
    EXPECT_EQ(Only(sink, ZoneToGateway::kSessionAttached).session_attached().gateway_session_id(),
              "s1");
}

TEST(ZoneHost, AttachWelcomesTheClientWithItsOwnEntityId) {
    sim::Zone zone = MakeZone();
    ZoneHost host{zone, Identity(), [] { return true; }};
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
    ZoneHost host{zone, Identity(1), [&] { return authoritative; }};
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

TEST(ZoneHost, InputReachesTheSimulationAndSnapshotsFollowCadence) {
    sim::Zone zone = MakeZone();
    ZoneHost host{zone, Identity(), [] { return true; }};
    FakeSink sink;
    host.Attach(sink, AttachFor("s1", 1));
    sink.sent.clear();

    internal::v1::SessionInput input;
    input.set_gateway_session_id("s1");
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
    ZoneHost host{zone, Identity(), [] { return true; }};
    FakeSink gateway_a;
    FakeSink gateway_b;
    host.Attach(gateway_a, AttachFor("a", 1));
    host.Attach(gateway_b, AttachFor("b", 2));
    gateway_a.sent.clear();

    TickAndSnapshot(zone, host);

    EXPECT_EQ(FirstSnapshotTo(gateway_a).spawned_size(), 1) << "a sees b";
}

TEST(ZoneHost, DetachDespawnsAndObserversAreTold) {
    sim::Zone zone = MakeZone();
    ZoneHost host{zone, Identity(), [] { return true; }};
    FakeSink gateway_a;
    FakeSink gateway_b;
    host.Attach(gateway_a, AttachFor("a", 1));
    host.Attach(gateway_b, AttachFor("b", 2));
    TickAndSnapshot(zone, host);

    internal::v1::SessionDetach detach;
    detach.set_gateway_session_id("b");
    host.Detach(detach);
    gateway_a.sent.clear();
    TickAndSnapshot(zone, host);

    EXPECT_EQ(FirstSnapshotTo(gateway_a).despawned_size(), 1) << "a is told b left";
    EXPECT_EQ(zone.stats().players, 1U);
    EXPECT_EQ(host.stats().detaches, 1U);
}

TEST(ZoneHost, LinkClosureAndEvictionRemoveEveryoneOnThatLink) {
    sim::Zone zone = MakeZone();
    ZoneHost host{zone, Identity(), [] { return true; }};
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
