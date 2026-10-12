#pragma once

#include "sa/common/opaque_id.hpp"
#include "sa/common/strong_id.hpp"

#include <cstdint>

// Identifier vocabulary shared by gateway, zone and loadgen. Mirrors the data ownership table
// in README "데이터의 기준".
namespace sa::common {

using WorldId = StrongId<struct WorldIdTag>;
using ZoneId = StrongId<struct ZoneIdTag, std::uint32_t>;
using AccountId = StrongId<struct AccountIdTag>;
using CharacterId = StrongId<struct CharacterIdTag>;
using EntityId = StrongId<struct EntityIdTag>;
using SessionId = StrongId<struct SessionIdTag>;

// Monotonic ownership generation for a character; it fences off stale owners. Higher wins.
using OwnerEpoch = StrongId<struct OwnerEpochTag>;

// Simulation tick counter of one zone process lifetime.
using TickIndex = StrongId<struct TickIndexTag>;

// Client-side monotonic request key within one session (contracts/game).
using RequestId = StrongId<struct RequestIdTag>;

// Minted identities that outlive a process (design document 9.2, 8.2): an item keeps its id from
// drop to inventory to trade; a kill id names one monster lifecycle's grant.
using ItemInstanceId = OpaqueId<struct ItemInstanceIdTag>;
using KillId = OpaqueId<struct KillIdTag>;

}  // namespace sa::common
