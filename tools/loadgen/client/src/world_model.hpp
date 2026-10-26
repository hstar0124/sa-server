#pragma once

#include "sa/bot/observation.hpp"
#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"

#include <game/server_messages.pb.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace sa::loadgen {

// Which command a request id was spent on, so its result lands in the right counter.
enum class RequestKind : std::uint8_t { kCast, kPickup, kChangeZone, kTrade };

// What one bot believes about the world, rebuilt from Welcome, WorldSnapshot, CommandResult and
// InventoryUpdate messages. It is also the consistency checker: a message that contradicts earlier
// ones is counted, because the server promised a coherent interest set (see README "Zone의 한
// 틱") and one final answer per request (see README "자산과 멱등성").
class WorldModel {
public:
    struct Combat {
        std::uint64_t casts_accepted = 0;
        std::uint64_t casts_rejected = 0;
        std::uint64_t casts_unknown = 0;  // the zone could not learn the outcome
        std::uint64_t hits_dealt = 0;
        std::uint64_t kills = 0;
        std::uint64_t deaths = 0;
    };

    struct Loot {
        std::uint64_t pickups_acquired = 0;
        std::uint64_t pickups_rejected = 0;  // final refusals: taken, reserved, full, not found
        std::uint64_t pickups_unknown = 0;   // the Backend did not answer; the bot may ask again
        std::uint32_t items_held = 0;        // from the last InventoryUpdate
        std::uint64_t gold = 0;
    };

    struct Travel {
        std::uint64_t zone_changes = 0;  // Welcomes after the first: the zone changed under us
        std::uint64_t transfers_accepted = 0;
        std::uint64_t transfers_rejected = 0;  // no destination now, already moving, out of range
        std::uint64_t transfers_unknown = 0;   // the Backend did not answer; asked again as is
    };

    struct Trades {
        std::uint64_t steps_accepted = 0;  // open, offer, accept or cancel the Backend applied
        std::uint64_t steps_rejected = 0;
        std::uint64_t steps_unknown = 0;  // the zone could not learn the outcome; sent again
        std::uint64_t settled = 0;        // TradeUpdates that closed a trade as completed
        std::uint64_t cancelled = 0;      // ... or as cancelled
    };

    struct Integrity {
        std::uint64_t despawn_of_unknown = 0;       // told to remove an entity never spawned
        std::uint64_t spawn_of_known = 0;           // told to spawn an entity already visible
        std::uint64_t update_of_unknown = 0;        // movement for an entity never spawned
        std::uint64_t self_in_view = 0;             // own entity appeared in the interest set
        std::uint64_t input_seq_regressed = 0;      // last_processed_input_seq went backwards
        std::uint64_t snapshot_before_welcome = 0;  // world state before the session was set up
        std::uint64_t server_tick_regressed = 0;    // a snapshot older than the previous one
        std::uint64_t self_mismatch = 0;  // the snapshot's own entity is not the welcomed one
        std::uint64_t result_of_unknown_request = 0;    // a verdict for a request never sent
        std::uint64_t inventory_version_regressed = 0;  // an older inventory after a newer one
        std::uint64_t trade_version_regressed = 0;      // an older state of a trade, or no valid id

        [[nodiscard]] std::uint64_t Total() const noexcept {
            return despawn_of_unknown + spawn_of_known + update_of_unknown + self_in_view +
                   input_seq_regressed + snapshot_before_welcome + server_tick_regressed +
                   self_mismatch + result_of_unknown_request + inventory_version_regressed +
                   trade_version_regressed;
        }
    };

    // A second Welcome means a transfer landed the session in another zone: what was visible
    // belonged to the old one, and the new zone acknowledges inputs and counts ticks afresh.
    void OnWelcome(const game::v1::Welcome& welcome);
    void OnSnapshot(const game::v1::WorldSnapshot& snapshot);
    // Registers a request the bot is about to send; its final result is counted under `kind`.
    void ExpectResult(common::RequestId request, RequestKind kind);
    void OnCommandResult(const game::v1::CommandResult& result);
    void OnCombatEvent(const game::v1::CombatEvent& event);
    void OnInventoryUpdate(const game::v1::InventoryUpdate& update);
    void OnTradeUpdate(const game::v1::TradeUpdate& update);

    [[nodiscard]] bool welcomed() const noexcept { return welcomed_; }
    [[nodiscard]] common::EntityId self() const noexcept { return self_; }
    [[nodiscard]] common::Vec2 self_position() const noexcept { return self_position_; }
    [[nodiscard]] common::Vec2 map_size() const noexcept { return map_size_; }
    [[nodiscard]] std::uint32_t last_acked_input() const noexcept { return last_acked_input_; }
    [[nodiscard]] std::uint64_t server_tick() const noexcept { return server_tick_; }
    [[nodiscard]] std::size_t visible_count() const noexcept { return visible_.size(); }
    [[nodiscard]] std::size_t max_visible() const noexcept { return max_visible_; }
    // Other players only: monsters, items and projectiles do not prove two clients met.
    [[nodiscard]] std::size_t max_visible_players() const noexcept { return max_visible_players_; }
    [[nodiscard]] std::uint32_t simulation_hz() const noexcept { return simulation_hz_; }
    [[nodiscard]] std::uint64_t snapshots() const noexcept { return snapshots_; }
    [[nodiscard]] const Integrity& integrity() const noexcept { return integrity_; }
    [[nodiscard]] const Combat& combat() const noexcept { return combat_; }
    [[nodiscard]] const Loot& loot() const noexcept { return loot_; }
    [[nodiscard]] const Travel& travel() const noexcept { return travel_; }
    [[nodiscard]] const Trades& trades() const noexcept { return trades_; }
    // The trade this bot is in, or the last closed one until the brain has seen it.
    [[nodiscard]] const std::optional<bot::SeenTrade>& trade() const noexcept { return trade_; }
    // After the brain has seen a closed trade it is forgotten, so the next decisions start afresh.
    void ForgetClosedTrade();
    [[nodiscard]] common::ZoneId zone() const noexcept { return zone_; }
    [[nodiscard]] const std::vector<bot::SeenPortal>& portals() const noexcept { return portals_; }
    // Requests still waiting for a final answer (a pickup the Backend is deciding, or one the run
    // ended before).
    [[nodiscard]] std::size_t awaiting_results() const noexcept { return awaiting_.size(); }
    [[nodiscard]] std::int32_t self_hp() const noexcept { return self_hp_; }
    [[nodiscard]] std::int32_t self_max_hp() const noexcept { return self_max_hp_; }

    // Snapshot of the visible set for the brain; valid until the next OnSnapshot.
    [[nodiscard]] const std::vector<bot::SeenEntity>& Visible();

private:
    bool welcomed_ = false;
    common::EntityId self_;
    common::Vec2 self_position_;
    common::Vec2 map_size_;
    std::uint32_t last_acked_input_ = 0;
    std::uint64_t server_tick_ = 0;
    std::uint64_t snapshots_ = 0;
    std::size_t max_visible_ = 0;
    std::size_t max_visible_players_ = 0;
    std::uint32_t simulation_hz_ = 0;
    common::ZoneId zone_;
    std::vector<bot::SeenPortal> portals_;
    // The state last reported for an entity. The brain sees it extrapolated with the velocity
    // to the latest server tick (dead reckoning), as the zone assumes every client does: an
    // entity walking straight is reported once.
    struct Known {
        bot::SeenKind kind = bot::SeenKind::kOther;
        common::Vec2 position;
        common::Vec2 velocity;
        std::uint64_t reported_tick = 0;
        std::int32_t hp = 0;
    };
    [[nodiscard]] common::Vec2 Extrapolated(const Known& known) const noexcept;
    std::unordered_map<common::EntityId, Known> visible_;
    std::unordered_map<common::RequestId, RequestKind> awaiting_;
    std::uint32_t inventory_version_ = 0;
    std::int32_t self_hp_ = 0;
    std::int32_t self_max_hp_ = 0;
    Combat combat_;
    Loot loot_;
    Travel travel_;
    Trades trades_;
    std::optional<bot::SeenTrade> trade_;
    std::vector<bot::SeenEntity> visible_cache_;
    bool cache_dirty_ = true;
    Integrity integrity_;
};

}  // namespace sa::loadgen
