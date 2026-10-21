// Drop rolls, ground items and pickups (see README "자산과 멱등성"). The zone rolls and reserves;
// the Backend decides; the host brings the answer back through SpawnGroundItems/ResolvePickup.
#include "sa/zone/sim/zone.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sa::zone::sim {

// ---- drops --------------------------------------------------------------------------------------

void Zone::RollDrops(common::EntityId killer, const Player& killer_player, const Monster& monster) {
    KillRecord record{.killer = killer,
                      .killer_character = killer_player.character,
                      .monster = monster.def->id,
                      .position = monster.position};
    if (const auto table = content_.drops.find(monster.def->id); table != content_.drops.end()) {
        const std::vector<DropEntry>& entries = table->second.entries;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            // Each entry is an independent roll; chance 1 always drops, chance 0 never does.
            if (rng_.NextUnit() >= entries[i].chance) {
                continue;
            }
            const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(i) /
                                static_cast<float>(entries.size());
            record.rolls.emplace_back(DropRoll{
                .item = entries[i].item,
                .position = monster.position + common::Vec2{std::cos(angle), std::sin(angle)} *
                                                   rules_.economy.drop_scatter_radius_m});
        }
    }
    stats_.drop_rolls += record.rolls.size();
    // The kill is recorded even without rolls: the table's gold is credited by the Backend.
    host_requests_.emplace_back(std::move(record));
}

std::vector<common::EntityId> Zone::SpawnGroundItems(std::span<const GroundItem> items) {
    std::vector<common::EntityId> ids;
    ids.reserve(items.size());
    for (const GroundItem& item : items) {
        const auto def = content_.items.find(item.item);
        if (def == content_.items.end()) {
            continue;  // content skew between zone and Backend; the host notices the shortfall
        }
        const common::EntityId id = AllocateEntityId();
        items_.emplace(id,
                       DroppedItem{.instance = item.instance,
                                   .def = &def->second,
                                   .position = item.position,
                                   .despawn_at = TicksFromNow(rules_.economy.ground_item_despawn)});
        kinds_.emplace(id, EntityKind::kDroppedItem);
        grid_.Insert(id, item.position);
        ids.push_back(id);
    }
    stats_.ground_items = items_.size();
    return ids;
}

void Zone::ExpireGroundItems() {
    std::vector<common::EntityId> expired;
    for (const auto& [id, item] : items_) {
        if (!item.reserved_by && tick_ >= item.despawn_at) {
            expired.push_back(id);
        }
    }
    for (const common::EntityId id : expired) {
        RemoveGroundItem(id);
    }
}

void Zone::RemoveGroundItem(common::EntityId id) {
    items_.erase(id);
    kinds_.erase(id);
    grid_.Remove(id);
    stats_.ground_items = items_.size();
}

std::vector<common::EntityId> Zone::GroundItems() const {
    std::vector<common::EntityId> out;
    out.reserve(items_.size());
    for (const auto& [id, _] : items_) {
        out.push_back(id);
    }
    std::ranges::sort(out);
    return out;
}

// ---- pickups ------------------------------------------------------------------------------------

void Zone::ApplyPickup(const PickupCommand& command) {
    const auto it = players_.find(command.player);
    if (it == players_.end()) {
        return;
    }
    Player& player = it->second;
    ++stats_.commands_applied;

    // A request without an id cannot be told apart from the next one; it is never remembered.
    if (command.request_id == common::RequestId{}) {
        Deliver(command.player,
                CommandResultEvent::Decided(command.request_id, RejectReason::kInvalidRequest));
        return;
    }
    // A retry of a pickup still waiting for the Backend is answered from its reservation, so no
    // number of other requests in between can make it run twice.
    const bool still_pending = std::ranges::any_of(items_, [&](const auto& entry) {
        const DroppedItem& item = entry.second;
        return item.reserved_by == command.player && item.reservation == command.request_id;
    });
    if (still_pending) {
        Deliver(command.player, CommandResultEvent::Pending(command.request_id));
        return;
    }
    // A decided request answers exactly as before.
    if (const auto previous = player.recent_requests.Find(command.request_id)) {
        Deliver(command.player, *previous);
        return;
    }

    const RejectReason reason = ValidatePickup(player, command.item);
    if (reason != RejectReason::kNone) {
        const auto result = CommandResultEvent::Decided(command.request_id, reason);
        player.recent_requests.Remember(result);
        Deliver(command.player, result);
        return;
    }

    DroppedItem& item = items_.at(command.item);
    item.reserved_by = command.player;
    item.reservation = command.request_id;
    Deliver(command.player, CommandResultEvent::Pending(command.request_id));
    host_requests_.emplace_back(PickupReservation{.player = command.player,
                                                  .character = player.character,
                                                  .request_id = command.request_id,
                                                  .item = command.item,
                                                  .instance = item.instance});
    ++stats_.pickups_requested;
}

RejectReason Zone::ValidatePickup(const Player& player, common::EntityId item_id) const {
    if (!player.vitals.IsAlive()) {
        return RejectReason::kDead;
    }
    const auto item = items_.find(item_id);
    if (item == items_.end()) {
        return RejectReason::kItemNotFound;
    }
    if (!common::WithinDistance(player.position, item->second.position,
                                rules_.economy.pickup_range_m)) {
        return RejectReason::kOutOfRange;
    }
    if (item->second.reserved_by) {
        return RejectReason::kItemReserved;
    }
    return RejectReason::kNone;
}

void Zone::ResolvePickup(const PickupReservation& reservation, PickupOutcome outcome) {
    const common::RequestId request = reservation.request_id;
    const auto it = items_.find(reservation.item);
    // Only this reservation is released: a released item may since have been reserved again.
    if (it != items_.end() && it->second.reserved_by == reservation.player &&
        it->second.reservation == request) {
        it->second.reserved_by.reset();
    }

    bool leaves_ground = false;
    CommandResultEvent result = CommandResultEvent::Unknown(request);
    switch (outcome) {
        case PickupOutcome::kAcquired:
            leaves_ground = true;
            result = CommandResultEvent::Decided(request, RejectReason::kNone);
            ++stats_.pickups_acquired;
            break;
        case PickupOutcome::kAlreadyTaken:
        case PickupOutcome::kUnknownItem:
            // Either way the Backend has no such item on this zone's ground any more.
            leaves_ground = true;
            result = CommandResultEvent::Decided(request, RejectReason::kItemNotFound);
            break;
        case PickupOutcome::kInventoryFull:
            result = CommandResultEvent::Decided(request, RejectReason::kInventoryFull);
            break;
        case PickupOutcome::kNotOwner:
            result = CommandResultEvent::Decided(request, RejectReason::kNotOwner);
            break;
        case PickupOutcome::kRequestMismatch:
            result = CommandResultEvent::Decided(request, RejectReason::kInvalidRequest);
            break;
        case PickupOutcome::kUnavailable:
            break;
    }
    if (leaves_ground && it != items_.end()) {
        RemoveGroundItem(reservation.item);
    }

    const auto player = players_.find(reservation.player);
    if (player == players_.end()) {
        return;  // the player left while the Backend was deciding
    }
    if (result.status != CommandStatus::kUnknown) {
        player->second.recent_requests.Remember(result);
    }
    Deliver(reservation.player, result);
}

void Zone::RemoveSettledGroundItems(std::span<const common::ItemInstanceId> held) {
    std::vector<common::EntityId> settled;
    for (const auto& [id, item] : items_) {
        if (!item.reserved_by && std::ranges::find(held, item.instance) != held.end()) {
            settled.push_back(id);
        }
    }
    for (const common::EntityId id : settled) {
        RemoveGroundItem(id);
    }
}

}  // namespace sa::zone::sim
