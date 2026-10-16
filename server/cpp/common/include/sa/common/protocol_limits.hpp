#pragma once

#include <game/framing.pb.h>

#include <cstddef>
#include <cstdint>

// Typed view of the protocol constants defined once in contracts/game/framing.proto.
namespace sa::common::protocol {

inline constexpr std::size_t kFrameHeaderBytes =
    static_cast<std::size_t>(sa::game::v1::FRAMING_HEADER_BYTES);
inline constexpr std::size_t kMaxPayloadBytes =
    static_cast<std::size_t>(sa::game::v1::FRAMING_MAX_PAYLOAD_BYTES);
inline constexpr sa::game::v1::ProtocolVersion kCurrentVersion =
    sa::game::v1::PROTOCOL_VERSION_CURRENT;

}  // namespace sa::common::protocol
