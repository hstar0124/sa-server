#pragma once

#include "sa/common/ids.hpp"
#include "sa/zone/sim/player_view.hpp"

#include <string>
#include <string_view>

namespace sa::zone {

// Writes one session's snapshot straight into the gateway link's wire format,
//   ZoneToGateway { session_output { gateway_session_id, ServerMessage { world_snapshot } } },
// without building protobuf message objects: byte for byte what codec::ToWorldSnapshot wrapped
// and serialized gives (the codec test checks it), at a fraction of the cost, since a snapshot
// otherwise builds an object per visible entity. A pure function of its inputs, so sessions can
// be encoded on worker threads in parallel.
void EncodeSnapshotOutput(std::string_view gateway_session_id, const sim::PlayerView& view,
                          common::TickIndex tick, std::string& out);

}  // namespace sa::zone
