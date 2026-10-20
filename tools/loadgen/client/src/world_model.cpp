#include "world_model.hpp"

#include <algorithm>

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
        default:
            return bot::SeenKind::kOther;
    }
}

}  // namespace

void WorldModel::OnWelcome(const game::v1::Welcome& welcome) {
    welcomed_ = true;
    self_ = common::EntityId{welcome.entity_id()};
    map_size_ = ToVec(welcome.map_size());
    server_tick_ = welcome.server_tick();
    simulation_hz_ = welcome.simulation_hz();
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

void WorldModel::OnCommandResult(const game::v1::CommandResult& result) {
    switch (result.status()) {
        case game::v1::COMMAND_STATUS_ACCEPTED:
            ++combat_.casts_accepted;
            return;
        case game::v1::COMMAND_STATUS_REJECTED:
            ++combat_.casts_rejected;
            return;
        case game::v1::COMMAND_STATUS_PENDING:
            ++combat_.results_pending;
            return;
        case game::v1::COMMAND_STATUS_UNKNOWN:
        default:
            ++combat_.results_unknown;
            return;
    }
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
