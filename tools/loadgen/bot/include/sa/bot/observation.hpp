#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace sa::bot {

enum class SeenKind : std::uint8_t { kPlayer, kMonster, kProjectile, kItem, kOther };

// What a bot knows about one nearby entity. Deliberately not the simulation's own type: the
// network bot fills this from protobuf snapshots.
struct SeenEntity {
    common::EntityId id;
    SeenKind kind = SeenKind::kOther;
    common::Vec2 position;
    std::int32_t hp = 0;
};

// A way out of the zone, as the zone's Welcome announced it.
struct SeenPortal {
    common::PortalId id;
    common::Vec2 position;
    float radius_m = 0.0F;  // ask to leave from inside this circle
    common::ZoneId to_zone;
};

enum class SeenTradeStatus : std::uint8_t { kOpen, kCompleted, kCancelled };

// The trade the bot is part of, as the last TradeUpdate described it (ADR-0013).
struct SeenTrade {
    common::TradeId id;
    SeenTradeStatus status = SeenTradeStatus::kOpen;
    std::uint32_t version = 0;  // grows with every offer; an acceptance names it
    common::EntityId other;     // nil when the counterparty is not in this zone
    std::uint64_t my_gold = 0;
    std::uint64_t other_gold = 0;
    bool i_accepted = false;
    bool other_accepted = false;
};

// Everything a brain is allowed to look at when deciding its next intent.
struct Observation {
    common::Vec2 self_position;
    common::Vec2 map_size;
    std::int32_t self_hp = 0;
    std::int32_t self_max_hp = 0;
    std::span<const SeenEntity> visible;
    std::span<const SeenPortal> portals;
    common::ZoneId zone;        // changes when a transfer lands the bot in another zone
    common::TickIndex tick;     // the current zone's tick; each zone counts its own
    std::uint32_t tick_hz = 0;  // rate of `tick`, i.e. the server's simulation rate; 0 until known
    std::uint64_t gold = 0;     // the wallet, from the last InventoryUpdate
    // The trade the bot is in, or one that just closed (seen once, then forgotten).
    std::optional<SeenTrade> trade;
    // A trade step the zone has not decided yet. The zone takes one at a time, so a brain sends
    // the next only after this clears; the client repeats the undecided one by itself.
    bool trade_step_pending = false;
};

// A wall-time interval in ticks of the given rate, rounded up so a bot never acts early. Profiles
// give intervals in wall time; brains compare them against the server's ticks.
[[nodiscard]] inline std::uint64_t TicksCovering(std::chrono::milliseconds interval,
                                                 std::uint32_t tick_hz) {
    const std::chrono::nanoseconds tick_period =
        std::chrono::nanoseconds{std::chrono::seconds{1}} / tick_hz;
    return static_cast<std::uint64_t>(
        (std::chrono::nanoseconds{interval} + tick_period - std::chrono::nanoseconds{1}) /
        tick_period);
}

// A skill use: targeted at an entity, or aimed along a direction.
struct CastIntent {
    common::SkillId skill;
    std::variant<common::EntityId, common::Vec2> target;
};

enum class TradeStep : std::uint8_t { kOpen, kOffer, kAccept, kCancel };

// One trade message: open with `target`, offer `gold` (and no items), accept `version`, or cancel.
struct TradeIntent {
    TradeStep step = TradeStep::kOpen;
    common::EntityId target;    // kOpen
    common::TradeId trade;      // kOffer, kAccept, kCancel
    std::uint64_t gold = 0;     // kOffer
    std::uint32_t version = 0;  // kAccept
};

// What the bot wants to do this tick. `direction` is a unit vector or zero (stand still).
struct Intent {
    common::Vec2 direction;
    std::optional<CastIntent> cast;
    std::optional<common::EntityId> pickup;       // a ground item to ask the zone for
    std::optional<common::PortalId> change_zone;  // a portal to leave through
    std::optional<TradeIntent> trade;
};

// A decision maker. Five live today: Wanderer, Fighter, Looter, Traveler and Trader; the network
// client picks one per bot from its profile, so the abstraction has real alternatives.
class Brain {
public:
    virtual ~Brain() = default;
    [[nodiscard]] virtual Intent Decide(const Observation& observation) = 0;
};

}  // namespace sa::bot
