#include "world_model.hpp"

#include <algorithm>

namespace sa::loadgen {
namespace {

common::Vec2 ToVec(const game::v1::Vec2& v) {
    return {v.x(), v.y()};
}

}  // namespace

void WorldModel::OnWelcome(const game::v1::Welcome& welcome) {
    welcomed_ = true;
    self_ = common::EntityId{welcome.entity_id()};
    map_size_ = ToVec(welcome.map_size());
    server_tick_ = welcome.server_tick();
}

void WorldModel::OnSnapshot(const game::v1::WorldSnapshot& snapshot) {
    ++snapshots_;
    server_tick_ = snapshot.server_tick();
    if (snapshot.last_processed_input_seq() < last_acked_input_) {
        ++integrity_.input_seq_regressed;
    }
    last_acked_input_ = std::max(last_acked_input_, snapshot.last_processed_input_seq());
    if (snapshot.has_self()) {
        self_position_ = ToVec(snapshot.self().position());
    }

    for (const game::v1::EntitySpawn& spawn : snapshot.spawned()) {
        const common::EntityId id{spawn.entity_id()};
        if (id == self_) {
            ++integrity_.self_in_view;
            continue;
        }
        if (!visible_.emplace(id, ToVec(spawn.position())).second) {
            ++integrity_.spawn_of_known;
        }
    }
    for (const game::v1::EntityUpdate& update : snapshot.updated()) {
        const auto it = visible_.find(common::EntityId{update.entity_id()});
        if (it == visible_.end()) {
            ++integrity_.update_of_unknown;
            continue;
        }
        it->second = ToVec(update.position());
    }
    for (const std::uint64_t raw : snapshot.despawned()) {
        if (visible_.erase(common::EntityId{raw}) == 0) {
            ++integrity_.despawn_of_unknown;
        }
    }
    max_visible_ = std::max(max_visible_, visible_.size());
    cache_dirty_ = true;
}

const std::vector<bot::SeenEntity>& WorldModel::Visible() {
    if (cache_dirty_) {
        visible_cache_.clear();
        visible_cache_.reserve(visible_.size());
        for (const auto& [id, position] : visible_) {
            visible_cache_.push_back(bot::SeenEntity{.id = id, .position = position});
        }
        cache_dirty_ = false;
    }
    return visible_cache_;
}

}  // namespace sa::loadgen
