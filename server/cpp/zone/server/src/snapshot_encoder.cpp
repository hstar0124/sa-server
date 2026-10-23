#include "snapshot_encoder.hpp"

#include "codec.hpp"

#include <google/protobuf/io/coded_stream.h>

#include <game/server_messages.pb.h>
#include <internal/zone_link.pb.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <vector>

namespace sa::zone {
namespace {

using google::protobuf::io::CodedOutputStream;

// Protobuf wire format (https://protobuf.dev/programming-guides/encoding/). Field numbers come from
// the generated classes, so a contract change that renumbers a field shows up here.
enum class Wire : std::uint8_t { kVarint = 0, kLengthDelimited = 2, kFixed32 = 5 };

constexpr std::uint32_t Tag(int field, Wire wire) {
    return (static_cast<std::uint32_t>(field) << 3U) | static_cast<std::uint32_t>(wire);
}

std::size_t VarintSize(std::uint64_t value) {
    return CodedOutputStream::VarintSize64(value);
}

// proto3 implicit presence: a scalar equal to its default is not written. For floats protobuf
// compares the bit pattern, so -0.0 is written; comparing with == 0.0F would drop it.
bool IsDefault(float value) {
    return std::bit_cast<std::uint32_t>(value) == 0;
}

std::size_t ScalarSize(int field, std::uint64_t value) {
    return value == 0 ? 0 : VarintSize(Tag(field, Wire::kVarint)) + VarintSize(value);
}
std::size_t FloatSize(int field, float value) {
    return IsDefault(value) ? 0 : VarintSize(Tag(field, Wire::kFixed32)) + sizeof(std::uint32_t);
}
std::size_t MessageSize(int field, std::size_t body) {
    return VarintSize(Tag(field, Wire::kLengthDelimited)) + VarintSize(body) + body;
}

std::size_t Vec2Body(common::Vec2 v) {
    return FloatSize(game::v1::Vec2::kXFieldNumber, v.x) +
           FloatSize(game::v1::Vec2::kYFieldNumber, v.y);
}

std::size_t UpdateBody(const sim::EntitySnapshot& s) {
    using U = game::v1::EntityUpdate;
    return ScalarSize(U::kEntityIdFieldNumber, s.id.value()) +
           MessageSize(U::kPositionFieldNumber, Vec2Body(s.position)) +
           MessageSize(U::kVelocityFieldNumber, Vec2Body(s.velocity)) +
           ScalarSize(U::kHpFieldNumber, codec::NonNegative(s.hp));
}

std::size_t SpawnBody(const sim::EntitySnapshot& s) {
    using S = game::v1::EntitySpawn;
    return ScalarSize(S::kEntityIdFieldNumber, s.id.value()) +
           ScalarSize(S::kKindFieldNumber, static_cast<std::uint64_t>(codec::ToProto(s.kind))) +
           ScalarSize(S::kTemplateIdFieldNumber, s.template_id) +
           MessageSize(S::kPositionFieldNumber, Vec2Body(s.position)) +
           ScalarSize(S::kHpFieldNumber, codec::NonNegative(s.hp)) +
           ScalarSize(S::kMaxHpFieldNumber, codec::NonNegative(s.max_hp)) +
           MessageSize(S::kVelocityFieldNumber, Vec2Body(s.velocity));
}

std::size_t VitalsBody(const sim::Vitals& v) {
    using V = game::v1::SelfVitals;
    return ScalarSize(V::kHpFieldNumber, codec::NonNegative(v.hp)) +
           ScalarSize(V::kMaxHpFieldNumber, codec::NonNegative(v.max_hp)) +
           ScalarSize(V::kMpFieldNumber, codec::NonNegative(v.mp)) +
           ScalarSize(V::kMaxMpFieldNumber, codec::NonNegative(v.max_mp));
}

// Packed repeated uint64: one length-delimited field holding the varints back to back.
std::size_t PackedBody(const std::vector<common::EntityId>& ids) {
    std::size_t body = 0;
    for (const common::EntityId id : ids) {
        body += VarintSize(id.value());
    }
    return body;
}

struct Sizes {
    std::size_t snapshot = 0;        // WorldSnapshot body
    std::size_t server_message = 0;  // ServerMessage body
    std::size_t session_output = 0;  // SessionOutput body
    std::size_t total = 0;           // ZoneToGateway body, the link payload
};

Sizes Measure(std::string_view session_id, const sim::PlayerView& view, common::TickIndex tick) {
    using W = game::v1::WorldSnapshot;
    Sizes sizes;
    sizes.snapshot =
        ScalarSize(W::kServerTickFieldNumber, tick.value()) +
        ScalarSize(W::kLastProcessedInputSeqFieldNumber, view.last_processed_input_seq);
    for (const sim::EntitySnapshot& s : view.spawned) {
        sizes.snapshot += MessageSize(W::kSpawnedFieldNumber, SpawnBody(s));
    }
    for (const sim::EntitySnapshot& s : view.updated) {
        sizes.snapshot += MessageSize(W::kUpdatedFieldNumber, UpdateBody(s));
    }
    if (!view.despawned.empty()) {
        sizes.snapshot += MessageSize(W::kDespawnedFieldNumber, PackedBody(view.despawned));
    }
    sizes.snapshot += MessageSize(W::kSelfFieldNumber, UpdateBody(view.self)) +
                      MessageSize(W::kSelfVitalsFieldNumber, VitalsBody(view.self_vitals));
    sizes.server_message =
        MessageSize(game::v1::ServerMessage::kWorldSnapshotFieldNumber, sizes.snapshot);
    sizes.session_output =
        (session_id.empty() ? 0
                            : MessageSize(internal::v1::SessionOutput::kGatewaySessionIdFieldNumber,
                                          session_id.size())) +
        MessageSize(internal::v1::SessionOutput::kMessageFieldNumber, sizes.server_message);
    sizes.total =
        MessageSize(internal::v1::ZoneToGateway::kSessionOutputFieldNumber, sizes.session_output);
    return sizes;
}

// ---- writers: each returns the advanced cursor --------------------------------------------

std::uint8_t* WriteVarint(std::uint64_t value, std::uint8_t* at) {
    return CodedOutputStream::WriteVarint64ToArray(value, at);
}
std::uint8_t* WriteScalar(int field, std::uint64_t value, std::uint8_t* at) {
    if (value == 0) {
        return at;
    }
    at = WriteVarint(Tag(field, Wire::kVarint), at);
    return WriteVarint(value, at);
}
std::uint8_t* WriteFloat(int field, float value, std::uint8_t* at) {
    if (IsDefault(value)) {
        return at;
    }
    at = WriteVarint(Tag(field, Wire::kFixed32), at);
    return CodedOutputStream::WriteLittleEndian32ToArray(std::bit_cast<std::uint32_t>(value), at);
}
std::uint8_t* WriteHeader(int field, std::size_t body, std::uint8_t* at) {
    at = WriteVarint(Tag(field, Wire::kLengthDelimited), at);
    return WriteVarint(body, at);
}
std::uint8_t* WriteVec2(int field, common::Vec2 v, std::uint8_t* at) {
    at = WriteHeader(field, Vec2Body(v), at);
    at = WriteFloat(game::v1::Vec2::kXFieldNumber, v.x, at);
    return WriteFloat(game::v1::Vec2::kYFieldNumber, v.y, at);
}
std::uint8_t* WriteUpdate(int field, const sim::EntitySnapshot& s, std::uint8_t* at) {
    using U = game::v1::EntityUpdate;
    at = WriteHeader(field, UpdateBody(s), at);
    at = WriteScalar(U::kEntityIdFieldNumber, s.id.value(), at);
    at = WriteVec2(U::kPositionFieldNumber, s.position, at);
    at = WriteVec2(U::kVelocityFieldNumber, s.velocity, at);
    return WriteScalar(U::kHpFieldNumber, codec::NonNegative(s.hp), at);
}
std::uint8_t* WriteSpawn(const sim::EntitySnapshot& s, std::uint8_t* at) {
    using S = game::v1::EntitySpawn;
    at = WriteHeader(game::v1::WorldSnapshot::kSpawnedFieldNumber, SpawnBody(s), at);
    at = WriteScalar(S::kEntityIdFieldNumber, s.id.value(), at);
    at = WriteScalar(S::kKindFieldNumber, static_cast<std::uint64_t>(codec::ToProto(s.kind)), at);
    at = WriteScalar(S::kTemplateIdFieldNumber, s.template_id, at);
    at = WriteVec2(S::kPositionFieldNumber, s.position, at);
    at = WriteScalar(S::kHpFieldNumber, codec::NonNegative(s.hp), at);
    at = WriteScalar(S::kMaxHpFieldNumber, codec::NonNegative(s.max_hp), at);
    return WriteVec2(S::kVelocityFieldNumber, s.velocity, at);
}
std::uint8_t* WriteVitals(const sim::Vitals& v, std::uint8_t* at) {
    using V = game::v1::SelfVitals;
    at = WriteHeader(game::v1::WorldSnapshot::kSelfVitalsFieldNumber, VitalsBody(v), at);
    at = WriteScalar(V::kHpFieldNumber, codec::NonNegative(v.hp), at);
    at = WriteScalar(V::kMaxHpFieldNumber, codec::NonNegative(v.max_hp), at);
    at = WriteScalar(V::kMpFieldNumber, codec::NonNegative(v.mp), at);
    return WriteScalar(V::kMaxMpFieldNumber, codec::NonNegative(v.max_mp), at);
}

}  // namespace

void EncodeSnapshotOutput(std::string_view gateway_session_id, const sim::PlayerView& view,
                          common::TickIndex tick, std::string& out) {
    using W = game::v1::WorldSnapshot;
    const Sizes sizes = Measure(gateway_session_id, view, tick);
    out.resize(sizes.total);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): protobuf writers take uint8_t*
    auto* at = reinterpret_cast<std::uint8_t*>(out.data());
    const std::uint8_t* const end = std::next(at, static_cast<std::ptrdiff_t>(sizes.total));

    at = WriteHeader(internal::v1::ZoneToGateway::kSessionOutputFieldNumber, sizes.session_output,
                     at);
    if (!gateway_session_id.empty()) {
        at = WriteHeader(internal::v1::SessionOutput::kGatewaySessionIdFieldNumber,
                         gateway_session_id.size(), at);
        std::memcpy(at, gateway_session_id.data(), gateway_session_id.size());
        at = std::next(at, static_cast<std::ptrdiff_t>(gateway_session_id.size()));
    }
    at = WriteHeader(internal::v1::SessionOutput::kMessageFieldNumber, sizes.server_message, at);
    at = WriteHeader(game::v1::ServerMessage::kWorldSnapshotFieldNumber, sizes.snapshot, at);
    at = WriteScalar(W::kServerTickFieldNumber, tick.value(), at);
    at = WriteScalar(W::kLastProcessedInputSeqFieldNumber, view.last_processed_input_seq, at);
    for (const sim::EntitySnapshot& s : view.spawned) {
        at = WriteSpawn(s, at);
    }
    for (const sim::EntitySnapshot& s : view.updated) {
        at = WriteUpdate(W::kUpdatedFieldNumber, s, at);
    }
    if (!view.despawned.empty()) {
        at = WriteHeader(W::kDespawnedFieldNumber, PackedBody(view.despawned), at);
        for (const common::EntityId id : view.despawned) {
            at = WriteVarint(id.value(), at);
        }
    }
    at = WriteUpdate(W::kSelfFieldNumber, view.self, at);
    at = WriteVitals(view.self_vitals, at);
    if (at != end) {
        std::abort();  // the size and write passes disagree: a programming error, never input
    }
}

}  // namespace sa::zone
