#pragma once

#include "sa/common/ids.hpp"
#include "sa/zone/sim/content.hpp"

#include <cstdint>
#include <variant>
#include <vector>

// Things the zone needs to tell clients that are not part of the periodic snapshot. The zone
// decides the recipients (see README "전투"); the host only serialises and routes.
namespace sa::zone::sim {

enum class RejectReason : std::uint8_t {
    kNone = 0,  // accepted
    kUnknownSkill,
    kNotLearned,
    kDead,
    kAlreadyCasting,
    kNotEnoughMp,
    kOnCooldown,
    kInvalidTarget,
    kOutOfRange,
};

struct CommandResultEvent {
    common::RequestId request_id;
    RejectReason reason = RejectReason::kNone;

    [[nodiscard]] constexpr bool accepted() const noexcept { return reason == RejectReason::kNone; }
};

struct CombatEvent {
    common::EntityId attacker;
    common::EntityId target;
    SkillId skill;  // none for a monster's basic attack
    std::int32_t damage = 0;
    std::int32_t target_hp = 0;  // after the hit
    bool killed = false;
};

using ZoneEvent = std::variant<CommandResultEvent, CombatEvent>;

struct DeliveredEvent {
    common::EntityId recipient;  // a player
    ZoneEvent event;
};

}  // namespace sa::zone::sim
