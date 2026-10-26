#include "snapshot_encoder.hpp"

#include "sa/common/instance_id.hpp"

#include "codec.hpp"

#include <gtest/gtest.h>

#include <internal/zone_link.pb.h>

#include <cstdint>
#include <limits>
#include <string>

namespace sa::zone {
namespace {

// What the host sent before this encoder: the codec's message, wrapped and serialized.
std::string ThroughProtobuf(const std::string& session_id, const sim::PlayerView& view,
                            common::TickIndex tick) {
    internal::v1::ZoneToGateway out;
    out.mutable_session_output()->set_gateway_session_id(session_id);
    *out.mutable_session_output()->mutable_message()->mutable_world_snapshot() =
        codec::ToWorldSnapshot(view, tick);
    return out.SerializeAsString();
}

std::string Direct(const std::string& session_id, const sim::PlayerView& view,
                   common::TickIndex tick) {
    std::string out;
    EncodeSnapshotOutput(session_id, view, tick, out);
    return out;
}

sim::EntitySnapshot Entity(std::uint64_t id, sim::EntityKind kind, common::Vec2 position,
                           common::Vec2 velocity, std::int32_t hp) {
    return sim::EntitySnapshot{.id = common::EntityId{id},
                               .kind = kind,
                               .template_id = static_cast<std::uint32_t>(id % 7),
                               .position = position,
                               .velocity = velocity,
                               .hp = hp,
                               .max_hp = 100};
}

const std::string kSession = common::InstanceId::Generate().ToWire();

TEST(SnapshotEncoder, AnEmptyViewMatchesTheProtobufBytes) {
    const sim::PlayerView view{.player = common::EntityId{1}};
    EXPECT_EQ(Direct(kSession, view, common::TickIndex{}), ThroughProtobuf(kSession, view, {}));
    EXPECT_EQ(Direct(kSession, view, common::TickIndex{42}),
              ThroughProtobuf(kSession, view, common::TickIndex{42}));
}

TEST(SnapshotEncoder, AFullViewMatchesTheProtobufBytesIncludingEdgeValues) {
    sim::PlayerView view{.player = common::EntityId{1}, .last_processed_input_seq = 300};
    view.self = Entity(1, sim::EntityKind::kPlayer, {12.5F, 0.0F}, {-0.0F, 6.0F}, 80);
    view.self_vitals = sim::Vitals{.hp = -5, .max_hp = 100, .mp = 0, .max_mp = 50};
    view.spawned = {
        Entity(2, sim::EntityKind::kMonster, {0.0F, 0.0F}, {0.0F, 0.0F}, 0),
        Entity(std::numeric_limits<std::uint64_t>::max(), sim::EntityKind::kDroppedItem,
               {199.99F, 0.001F}, {}, 1),
        Entity(300, sim::EntityKind::kProjectile, {-0.0F, 5.0F}, {25.0F, -0.0F}, -20),
    };
    view.updated = {
        Entity(4, sim::EntityKind::kPlayer, {50.0F, 50.0F}, {-6.0F, 0.0F}, 100),
        Entity(5, sim::EntityKind::kMonster, {1.0F, 2.0F}, {0.0F, -0.0F}, 3),
    };
    view.despawned = {common::EntityId{6}, common::EntityId{1U << 20U}, common::EntityId{7}};
    const common::TickIndex tick{123'456'789};
    EXPECT_EQ(Direct(kSession, view, tick), ThroughProtobuf(kSession, view, tick));
}

// -0.0 compares equal to 0.0 but protobuf writes it (presence is by bit pattern), so an encoder
// that tested `== 0.0F` would differ exactly here.
TEST(SnapshotEncoder, NegativeZeroIsWrittenLikeProtobufWritesIt) {
    sim::PlayerView view{.player = common::EntityId{1}};
    view.self = Entity(1, sim::EntityKind::kPlayer, {}, {-0.0F, -0.0F}, 1);
    const std::string direct = Direct(kSession, view, common::TickIndex{1});
    EXPECT_EQ(direct, ThroughProtobuf(kSession, view, common::TickIndex{1}));
    sim::PlayerView positive = view;
    positive.self.velocity = {};
    EXPECT_GT(direct.size(), Direct(kSession, positive, common::TickIndex{1}).size());
}

}  // namespace
}  // namespace sa::zone
