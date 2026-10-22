#include "world_model.hpp"

#include <algorithm>
#include <cstdint>

namespace sa::loadgen {
namespace {

common::Vec2 ToVec(const game::v1::Vec2& v) {
    return {v.x(), v.y()};
}

bot::SeenKind ToKind(game::v1::EntityKind kind) {
    switch (kind) {
        case game::v1::ENTITY_KIND_PLAYER:
            return bot::SeenKind::kPlayer;
        case game::v1::ENTITY_KIND_MONSTER:
            return bot::SeenKind::kMonster;
        case game::v1::ENTITY_KIND_PROJECTILE:
            return bot::SeenKind::kProjectile;
        case game::v1::ENTITY_KIND_DROPPED_ITEM:
            return bot::SeenKind::kItem;
        default:
            return bot::SeenKind::kOther;
    }
}

// The counter a final verdict lands in. UNKNOWN is not a refusal: the zone could not learn the
// outcome, and the same request may still succeed when asked again.
std::uint64_t& VerdictCounter(game::v1::CommandStatus status, std::uint64_t& accepted,
                              std::uint64_t& rejected, std::uint64_t& unknown) {
    if (status == game::v1::COMMAND_STATUS_ACCEPTED) {
        return accepted;
    }
    return status == game::v1::COMMAND_STATUS_UNKNOWN ? unknown : rejected;
}

}  // namespace

void WorldModel::OnWelcome(const game::v1::Welcome& welcome) {
    if (welcomed_) {
        ++travel_.zone_changes;
        visible_.clear();
        cache_dirty_ = true;
        last_acked_input_ = 0;
    }
    welcomed_ = true;
    self_ = common::EntityId{welcome.entity_id()};
    zone_ = common::ZoneId{welcome.zone_id()};
    map_size_ = ToVec(welcome.map_size());
    server_tick_ = welcome.server_tick();
    simulation_hz_ = welcome.simulation_hz();
    portals_.clear();
    for (const game::v1::Portal& portal : welcome.portals()) {
        portals_.push_back(bot::SeenPortal{.id = common::PortalId{portal.portal_id()},
                                           .position = ToVec(portal.position()),
                                           .radius_m = portal.radius_m(),
                                           .to_zone = common::ZoneId{portal.to_zone_id()}});
    }
}

void WorldModel::OnSnapshot(const game::v1::WorldSnapshot& snapshot) {
    ++snapshots_;
    if (!welcomed_) {
        ++integrity_.snapshot_before_welcome;
    }
    if (snapshots_ > 1 && snapshot.server_tick() <= server_tick_) {
        ++integrity_.server_tick_regressed;
    }
    server_tick_ = snapshot.server_tick();
    if (snapshot.last_processed_input_seq() < last_acked_input_) {
        ++integrity_.input_seq_regressed;
    }
    last_acked_input_ = std::max(last_acked_input_, snapshot.last_processed_input_seq());
    if (snapshot.has_self()) {
        if (welcomed_ && common::EntityId{snapshot.self().entity_id()} != self_) {
            ++integrity_.self_mismatch;
        }
        self_position_ = ToVec(snapshot.self().position());
    }
    if (snapshot.has_self_vitals()) {
        self_hp_ = static_cast<std::int32_t>(snapshot.self_vitals().hp());
        self_max_hp_ = static_cast<std::int32_t>(snapshot.self_vitals().max_hp());
    }

    for (const game::v1::EntitySpawn& spawn : snapshot.spawned()) {
        const common::EntityId id{spawn.entity_id()};
        if (id == self_) {
            ++integrity_.self_in_view;
            continue;
        }
        const Known known{.kind = ToKind(spawn.kind()),
                          .position = ToVec(spawn.position()),
                          .hp = static_cast<std::int32_t>(spawn.hp())};
        if (!visible_.emplace(id, known).second) {
            ++integrity_.spawn_of_known;
        }
    }
    for (const game::v1::EntityUpdate& update : snapshot.updated()) {
        const auto it = visible_.find(common::EntityId{update.entity_id()});
        if (it == visible_.end()) {
            ++integrity_.update_of_unknown;
            continue;
        }
        it->second.position = ToVec(update.position());
        it->second.hp = static_cast<std::int32_t>(update.hp());
    }
    for (const std::uint64_t raw : snapshot.despawned()) {
        if (visible_.erase(common::EntityId{raw}) == 0) {
            ++integrity_.despawn_of_unknown;
        }
    }
    max_visible_ = std::max(max_visible_, visible_.size());
    const auto players = static_cast<std::size_t>(std::ranges::count_if(
        visible_, [](const auto& entry) { return entry.second.kind == bot::SeenKind::kPlayer; }));
    max_visible_players_ = std::max(max_visible_players_, players);
    cache_dirty_ = true;
}

void WorldModel::ExpectResult(common::RequestId request, RequestKind kind) {
    awaiting_.insert_or_assign(request, kind);
}

void WorldModel::OnCommandResult(const game::v1::CommandResult& result) {
    const auto awaiting = awaiting_.find(common::RequestId{result.request_id()});
    if (awaiting == awaiting_.end()) {
        ++integrity_.result_of_unknown_request;
        return;
    }
    if (result.status() == game::v1::COMMAND_STATUS_PENDING) {
        return;  // the Backend is still deciding; the final verdict follows
    }
    const RequestKind kind = awaiting->second;
    awaiting_.erase(awaiting);
    switch (kind) {
        case RequestKind::kCast:
            ++VerdictCounter(result.status(), combat_.casts_accepted, combat_.casts_rejected,
                             combat_.casts_unknown);
            return;
        case RequestKind::kPickup:
            ++VerdictCounter(result.status(), loot_.pickups_acquired, loot_.pickups_rejected,
                             loot_.pickups_unknown);
            return;
        case RequestKind::kChangeZone:
            ++VerdictCounter(result.status(), travel_.transfers_accepted,
                             travel_.transfers_rejected, travel_.transfers_unknown);
            return;
    }
}

void WorldModel::OnInventoryUpdate(const game::v1::InventoryUpdate& update) {
    if (update.version() < inventory_version_) {
        ++integrity_.inventory_version_regressed;
        return;  // an older state must not overwrite a newer one
    }
    inventory_version_ = update.version();
    loot_.items_held = static_cast<std::uint32_t>(update.items_size());
    loot_.gold = update.gold();
}

void WorldModel::OnCombatEvent(const game::v1::CombatEvent& event) {
    const common::EntityId attacker{event.attacker_id()};
    const common::EntityId target{event.target_id()};
    if (attacker == self_) {
        ++combat_.hits_dealt;
        if (event.killed()) {
            ++combat_.kills;
        }
    }
    if (target == self_ && event.killed()) {
        ++combat_.deaths;
    }
}

const std::vector<bot::SeenEntity>& WorldModel::Visible() {
    if (cache_dirty_) {
        visible_cache_.clear();
        visible_cache_.reserve(visible_.size());
        for (const auto& [id, known] : visible_) {
            visible_cache_.push_back(bot::SeenEntity{
                .id = id, .kind = known.kind, .position = known.position, .hp = known.hp});
        }
        cache_dirty_ = false;
    }
    return visible_cache_;
}

}  // namespace sa::loadgen
