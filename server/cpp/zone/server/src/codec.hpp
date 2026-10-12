#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"
#include "sa/zone/sim/entities.hpp"
#include "sa/zone/sim/player_view.hpp"

#include <game/client_messages.pb.h>
#include <game/server_messages.pb.h>

#include <cstdint>
#include <optional>

// Protobuf <-> simulation conversions (ADR-0008: the simulation never sees protobuf). Pure
// functions; tested without sockets.
namespace sa::zone::codec {

[[nodiscard]] sim::MoveCommand ToMoveCommand(common::EntityId player,
                                             const game::v1::MoveInput& input);

[[nodiscard]] game::v1::WorldSnapshot ToWorldSnapshot(const sim::PlayerView& view,
                                                      const sim::EntitySnapshot& self,
                                                      common::TickIndex tick);

struct WelcomeInfo {
    common::CharacterId character;
    common::ZoneId zone;
    std::uint32_t simulation_hz = 0;
    common::TickIndex tick;
    common::EntityId entity;
    common::Vec2 map_size;
};
[[nodiscard]] game::v1::Welcome ToWelcome(const WelcomeInfo& info);

[[nodiscard]] game::v1::EntityKind ToProto(sim::EntityKind kind);

}  // namespace sa::zone::codec
