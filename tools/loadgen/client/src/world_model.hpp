#pragma once

#include "sa/bot/observation.hpp"
#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"

#include <game/server_messages.pb.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sa::loadgen {

// What one bot believes about the world, rebuilt from Welcome and WorldSnapshot messages. It is
// also the consistency checker: a snapshot that contradicts earlier ones is counted, because the
// server promised a coherent interest set (see README "Zone의 한 틱").
class WorldModel {
public:
    struct Combat {
        std::uint64_t casts_accepted = 0;
        std::uint64_t casts_rejected = 0;
        std::uint64_t hits_dealt = 0;
        std::uint64_t kills = 0;
        std::uint64_t deaths = 0;
    };

    struct Integrity {
        std::uint64_t despawn_of_unknown = 0;   // told to remove an entity never spawned
        std::uint64_t spawn_of_known = 0;       // told to spawn an entity already visible
        std::uint64_t update_of_unknown = 0;    // movement for an entity never spawned
        std::uint64_t self_in_view = 0;         // own entity appeared in the interest set
        std::uint64_t input_seq_regressed = 0;  // last_processed_input_seq went backwards

        [[nodiscard]] std::uint64_t Total() const noexcept {
            return despawn_of_unknown + spawn_of_known + update_of_unknown + self_in_view +
                   input_seq_regressed;
        }
    };

    void OnWelcome(const game::v1::Welcome& welcome);
    void OnSnapshot(const game::v1::WorldSnapshot& snapshot);
    void OnCommandResult(const game::v1::CommandResult& result);
    void OnCombatEvent(const game::v1::CombatEvent& event);

    [[nodiscard]] bool welcomed() const noexcept { return welcomed_; }
    [[nodiscard]] common::EntityId self() const noexcept { return self_; }
    [[nodiscard]] common::Vec2 self_position() const noexcept { return self_position_; }
    [[nodiscard]] common::Vec2 map_size() const noexcept { return map_size_; }
    [[nodiscard]] std::uint32_t last_acked_input() const noexcept { return last_acked_input_; }
    [[nodiscard]] std::uint64_t server_tick() const noexcept { return server_tick_; }
    [[nodiscard]] std::size_t visible_count() const noexcept { return visible_.size(); }
    [[nodiscard]] std::size_t max_visible() const noexcept { return max_visible_; }
    [[nodiscard]] std::uint64_t snapshots() const noexcept { return snapshots_; }
    [[nodiscard]] const Integrity& integrity() const noexcept { return integrity_; }
    [[nodiscard]] const Combat& combat() const noexcept { return combat_; }
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
    struct Known {
        bot::SeenKind kind = bot::SeenKind::kOther;
        common::Vec2 position;
        std::int32_t hp = 0;
    };
    std::unordered_map<common::EntityId, Known> visible_;
    std::int32_t self_hp_ = 0;
    std::int32_t self_max_hp_ = 0;
    Combat combat_;
    std::vector<bot::SeenEntity> visible_cache_;
    bool cache_dirty_ = true;
    Integrity integrity_;
};

}  // namespace sa::loadgen
