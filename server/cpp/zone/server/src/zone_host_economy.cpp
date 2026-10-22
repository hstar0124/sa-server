// Economy round trips (see README "자산과 멱등성"): the character's assets, kills and pickups
// settled with the Backend and mirrored to the client.
#include "codec.hpp"
#include "zone_host.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <vector>

namespace sa::zone {

void ZoneHost::RequestAssets(const common::InstanceId& session_id) {
    const Session& session = sessions_.at(session_id);
    backend::v1::GetCharacterAssetsRequest request;
    request.set_world_id(zone_.config().world_id.value());
    request.set_character_id(session.character.value());
    economy_.GetCharacterAssets(
        request, [this, session_id, entity = session.entity](
                     const std::optional<backend::v1::GetCharacterAssetsResponse>& response) {
            OnAssets(session_id, entity, response);
        });
}

void ZoneHost::OnAssets(const common::InstanceId& session_id, common::EntityId entity,
                        const std::optional<backend::v1::GetCharacterAssetsResponse>& response) {
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end() || it->second.entity != entity) {
        return;  // the session ended while the Backend was answering
    }
    if (!response) {
        ++stats_.backend_failures;
        spdlog::warn("assets of character {} unavailable; asking again on the next economy answer",
                     it->second.character.value());
        return;
    }
    it->second.assets = codec::ToAssets(*response, zone_.rules().economy.primary_currency);
    // A pickup that committed while its answer was lost leaves the item visible here although
    // the Backend holds it in this inventory; the fresh read settles it.
    std::vector<common::ItemInstanceId> held;
    held.reserve(it->second.assets->items.size());
    for (const codec::InventoryEntry& entry : it->second.assets->items) {
        held.push_back(entry.instance);
    }
    zone_.RemoveSettledGroundItems(held);
    SendAssets(session_id, it->second);
}

void ZoneHost::RecordKill(const sim::KillRecord& kill) {
    const Session* session = SessionOf(kill.killer);
    if (session == nullptr) {
        return;  // the killer left before the kill was recorded; the grant cannot be fenced
    }
    const common::KillId kill_id = codec::KillIdFor(identity_.zone_instance, ++kills_minted_);
    economy_.RecordKill(codec::ToRecordKillRequest(kill, kill_id, zone_.config().world_id,
                                                   AuthorityFor(*session), zone_.content().version),
                        [this, killer = kill.killer,
                         kill_id](const std::optional<backend::v1::RecordKillResponse>& response) {
                            OnKillRecorded(killer, kill_id, response);
                        });
}

void ZoneHost::OnKillRecorded(common::EntityId killer, common::KillId kill_id,
                              const std::optional<backend::v1::RecordKillResponse>& response) {
    const auto by_entity = session_by_entity_.find(killer);
    const common::InstanceId* session_id =
        by_entity == session_by_entity_.end() ? nullptr : &by_entity->second;
    if (!response) {
        // The last attempt may have timed out after the Backend committed: the gold may be in the
        // wallet and the drops on the Backend's ground. Re-read the killer's assets so the client
        // at least sees the wallet; the kill id is kept in the log for reconciliation.
        ++stats_.backend_failures;
        spdlog::warn("kill {}: no answer after every attempt; outcome unknown", kill_id.ToHex());
        if (session_id != nullptr) {
            RequestAssets(*session_id);
        }
        return;
    }
    switch (response->outcome()) {
        case backend::v1::KILL_OUTCOME_RECORDED:
        case backend::v1::KILL_OUTCOME_ALREADY_RECORDED: {
            const std::vector<sim::GroundItem> items = codec::ToGroundItems(*response);
            const auto spawned = zone_.SpawnGroundItems(items);
            if (spawned.size() != items.size()) {
                spdlog::warn("kill {}: {} of {} items name templates this zone does not know",
                             kill_id.ToHex(), items.size() - spawned.size(), items.size());
            }
            ++stats_.kills_recorded;
            break;
        }
        case backend::v1::KILL_OUTCOME_STALE_AUTHORITY:
            ++stats_.kills_refused;
            spdlog::warn("kill {} refused: this zone does not own the killer", kill_id.ToHex());
            if (session_id != nullptr) {
                EvictNotOwner(*session_id);
            }
            return;
        case backend::v1::KILL_OUTCOME_CONTENT_VERSION_MISMATCH:
        case backend::v1::KILL_OUTCOME_UNKNOWN_TEMPLATE:
        case backend::v1::KILL_OUTCOME_UNSPECIFIED:
        default:
            ++stats_.kills_refused;
            spdlog::warn("kill {} refused: {}", kill_id.ToHex(),
                         backend::v1::KillOutcome_Name(response->outcome()));
            return;
    }
    if (session_id == nullptr) {
        return;
    }
    Session& session = sessions_.at(*session_id);
    if (!session.assets) {
        RequestAssets(*session_id);
        return;
    }
    // Gold only rises in this stage (kills credit, nothing spends), so the larger balance is the
    // newer one even when two kill answers arrive out of order. Spending needs a wallet version.
    session.assets->gold = std::max(session.assets->gold, response->gold_balance());
    SendAssets(*session_id, session);
}

void ZoneHost::SettlePickup(const sim::PickupReservation& pickup) {
    const Session* session = SessionOf(pickup.player);
    if (session == nullptr) {
        zone_.ResolvePickup(pickup, sim::PickupOutcome::kUnavailable);
        return;
    }
    economy_.PickupItem(
        codec::ToPickupItemRequest(pickup, zone_.config().world_id, AuthorityFor(*session)),
        [this, pickup](const std::optional<backend::v1::PickupItemResponse>& response) {
            OnPickupSettled(pickup, response);
        });
}

void ZoneHost::OnPickupSettled(const sim::PickupReservation& pickup,
                               const std::optional<backend::v1::PickupItemResponse>& response) {
    const auto by_entity = session_by_entity_.find(pickup.player);
    const common::InstanceId* session_id =
        by_entity == session_by_entity_.end() ? nullptr : &by_entity->second;
    if (!response) {
        // Unknown even after the client's retries: release the reservation, and read the assets
        // so that an item the Backend did commit disappears here and shows in the inventory.
        ++stats_.backend_failures;
        zone_.ResolvePickup(pickup, sim::PickupOutcome::kUnavailable);
        if (session_id != nullptr) {
            RequestAssets(*session_id);
        }
        return;
    }
    const sim::PickupOutcome outcome = codec::ToPickupOutcome(response->outcome());
    zone_.ResolvePickup(pickup, outcome);
    if (outcome != sim::PickupOutcome::kAcquired) {
        ++stats_.pickups_refused;
        if (outcome == sim::PickupOutcome::kNotOwner && session_id != nullptr) {
            EvictNotOwner(*session_id);
        } else if (outcome == sim::PickupOutcome::kUnavailable ||
                   outcome == sim::PickupOutcome::kRequestMismatch) {
            spdlog::warn("pickup by character {} not settled: {}", pickup.character.value(),
                         backend::v1::PickupOutcome_Name(response->outcome()));
        }
        return;
    }
    ++stats_.pickups_acquired;
    if (session_id == nullptr) {
        return;
    }
    Session& session = sessions_.at(*session_id);
    if (!session.assets) {
        RequestAssets(*session_id);  // the first read failed; this answer proves the Backend is up
        return;
    }
    if (const auto entry = codec::ToInventoryEntry(response->item())) {
        const bool known = std::ranges::any_of(session.assets->items, [&](const auto& item) {
            return item.instance == entry->instance;
        });
        if (!known) {
            session.assets->items.push_back(*entry);
        }
    }
    // Two pickups in flight may answer out of order; a version never goes back.
    session.assets->version = std::max(session.assets->version, response->inventory_version());
    SendAssets(*session_id, session);
}

void ZoneHost::EvictNotOwner(const common::InstanceId& session_id) {
    // Another zone owns the character now (a re-entry elsewhere, or this zone's lease lapsed):
    // nothing this session does here can be granted, so it ends and the client enters again.
    ++stats_.not_owner_evictions;
    Evict(session_id, internal::v1::EVICT_REASON_NOT_OWNER);
}

void ZoneHost::SendAssets(const common::InstanceId& session_id, Session& session) {
    if (!session.assets) {
        return;
    }
    game::v1::ServerMessage message;
    *message.mutable_inventory_update() = codec::ToInventoryUpdate(*session.assets);
    SendToSession(session, session_id, message);
    ++stats_.inventory_updates_sent;
}

}  // namespace sa::zone
