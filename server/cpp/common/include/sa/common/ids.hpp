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

// Content ids from game-data that cross processes: a character's class comes from the Backend,
// and a client names the skill it casts.
using ClassId = StrongId<struct ClassIdTag, std::uint32_t>;
using SkillId = StrongId<struct SkillIdTag, std::uint32_t>;
// A way out of a zone, numbered within its map (game-data/maps).
using PortalId = StrongId<struct PortalIdTag, std::uint32_t>;

// Simulation tick counter of one zone process lifetime.
using TickIndex = StrongId<struct TickIndexTag>;

// Client-side monotonic request key within one session (contracts/game).
using RequestId = StrongId<struct RequestIdTag>;

// Minted identities that outlive a process (see README "자산과 멱등성"): an item keeps its id from
// drop to inventory to trade; a kill id names one monster lifecycle's grant.
using ItemInstanceId = OpaqueId<struct ItemInstanceIdTag>;
using KillId = OpaqueId<struct KillIdTag>;
// Idempotency key the Backend stores for one zone-originated command (e.g. a pickup).
using RequestKey = OpaqueId<struct RequestKeyTag>;
// One attempt to hand a character to another zone; the Backend stores its outcome (ADR-0010).
using TransferId = OpaqueId<struct TransferIdTag>;

}  // namespace sa::common
