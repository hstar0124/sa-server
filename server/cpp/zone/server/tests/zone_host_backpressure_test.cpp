#include "zone_host_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sa::zone {
namespace {

using internal::v1::ZoneToGateway;
using namespace host_fixture;  // NOLINT(google-build-using-namespace): test fixtures only

// The snapshots each session got, by the session's wire id, in the order they were sent.
std::vector<std::string> SnapshotRecipients(const FakeSink& sink) {
    std::vector<std::string> out;
    for (const ZoneToGateway& m : sink.sent) {
        if (m.has_session_output() && m.session_output().message().has_world_snapshot()) {
            out.push_back(m.session_output().gateway_session_id());
        }
    }
    return out;
}

// One snapshot tick: the identity snapshots every second tick.
void SnapshotTick(sim::Zone& zone, ZoneHost& host) {
    zone.Tick();
    host.OnTick(zone.current_tick());
    zone.Tick();
    host.OnTick(zone.current_tick());
}

class ZoneHostBackpressure : public ::testing::Test {
protected:
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    FakeTrades trades;
    ZoneHost host{zone, Identity(), economy, directory, trades, [] { return true; }};
};

TEST_F(ZoneHostBackpressure, ACongestedLinkDefersItsSnapshotsAndTheNextOneLosesNothing) {
    FakeSink sink;
    host.Attach(sink, AttachFor("s1", 1));
    host.Attach(sink, AttachFor("s2", 2));
    std::map<std::string, std::uint64_t> entity_of;
    for (const ZoneToGateway& m : sink.OfCase(ZoneToGateway::kSessionAttached)) {
        entity_of[m.session_attached().gateway_session_id()] = m.session_attached().entity_id();
    }
    sink.sent.clear();
    sink.queued = kTestSnapshotBudget;  // the gateway is not keeping up

    SnapshotTick(zone, host);
    EXPECT_TRUE(SnapshotRecipients(sink).empty());
    EXPECT_EQ(host.stats().snapshots_deferred, 2U);

    sink.queued = 0;  // drained
    SnapshotTick(zone, host);
    ASSERT_EQ(SnapshotRecipients(sink).size(), 2U);
    std::size_t snapshot_bytes = 0;
    for (const ZoneToGateway& m : sink.OfCase(ZoneToGateway::kSessionOutput)) {
        if (!m.session_output().message().has_world_snapshot()) {
            continue;
        }
        snapshot_bytes += m.ByteSizeLong();
        const std::string& me = m.session_output().gateway_session_id();
        const std::uint64_t other =
            entity_of.at(me == SessionWire("s1") ? SessionWire("s2") : SessionWire("s1"));
        const auto& spawned = m.session_output().message().world_snapshot().spawned();
        EXPECT_TRUE(std::ranges::any_of(spawned, [&](const auto& e) {
            return e.entity_id() == other;
        })) << "the other player, held back with the view, arrives now";
    }
    EXPECT_EQ(host.stats().snapshot_bytes, snapshot_bytes) << "only what reached the link counts";
}

TEST_F(ZoneHostBackpressure, WithinTheBudgetTheLongestWaitingSessionsGoFirst) {
    FakeSink sink;
    host.Attach(sink, AttachFor("s1", 1));
    host.Attach(sink, AttachFor("s2", 2));
    host.Attach(sink, AttachFor("s3", 3));
    sink.sent.clear();

    // Room for one snapshot per tick: the first one sent pushes the link over its budget.
    std::vector<std::string> order;
    for (int tick = 0; tick < 3; ++tick) {
        sink.queued = kTestSnapshotBudget - 1;
        sink.sent.clear();
        SnapshotTick(zone, host);
        const auto got = SnapshotRecipients(sink);
        ASSERT_EQ(got.size(), 1U) << "tick " << tick;
        order.push_back(got.front());
    }
    EXPECT_EQ(order, (std::vector{SessionWire("s1"), SessionWire("s2"), SessionWire("s3")}))
        << "each tick the session that waited longest; nobody is starved";
    EXPECT_EQ(host.stats().snapshots_deferred, 3U * 2U) << "two wait every tick";
}

TEST_F(ZoneHostBackpressure, EachLinkHasItsOwnBudget) {
    FakeSink slow;
    FakeSink fast;
    host.Attach(slow, AttachFor("slow", 1));
    host.Attach(fast, AttachFor("fast", 2));
    slow.queued = kTestSnapshotBudget;

    SnapshotTick(zone, host);
    EXPECT_TRUE(SnapshotRecipients(slow).empty());
    EXPECT_EQ(SnapshotRecipients(fast), std::vector{SessionWire("fast")})
        << "one gateway falling behind does not hold back another";
}

}  // namespace
}  // namespace sa::zone
