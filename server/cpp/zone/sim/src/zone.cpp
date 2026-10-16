#include "sa/zone/sim/zone.hpp"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <ranges>

namespace sa::zone::sim {
namespace {

// Half-width of the player's footprint used for terrain tests; kept out of game-data because it
// is a property of the collision model, not of the content.
constexpr float kPlayerRadiusM = 0.4F;

bool SortedContains(const std::vector<common::EntityId>& sorted, common::EntityId id) {
    return std::ranges::binary_search(sorted, id);
}

}  // namespace

Zone::Zone(ZoneConfig config, MapData map, MovementRules rules, Rng rng)
    : config_(config),
      map_(std::move(map)),
      rules_(rules),
      rng_(rng),
      dt_seconds_(std::chrono::duration<float>(config.simulation_rate.Period()).count()),
      grid_(map_.size, map_.cell_size) {}

// ---- membership -----------------------------------------------------------------------------

common::EntityId Zone::SpawnPlayer(common::CharacterId character, std::uint32_t class_id,
                                   std::optional<common::Vec2> position) {
    const common::EntityId id = next_entity_id_;
    next_entity_id_ = next_entity_id_.Next();

    common::Vec2 at = position.value_or(common::Vec2{});
    if (!position) {
        const auto index = rng_.Range<std::size_t>(0, map_.spawn_points.size() - 1);
        at = map_.spawn_points[index];
    }
    players_.emplace(id, Player{.character = character, .class_id = class_id, .position = at});
    grid_.Insert(id, at);
    stats_.players = players_.size();
    return id;
}

void Zone::DespawnPlayer(common::EntityId player) {
    if (players_.erase(player) == 0) {
        return;
    }
    grid_.Remove(player);
    stats_.players = players_.size();
    // Observers learn about the departure through their next interest recomputation.
}

// ---- input ----------------------------------------------------------------------------------

void Zone::Submit(MoveCommand command) {
    pending_.push_back(command);
}

void Zone::DrainCommands() {
    std::uint32_t budget = rules_.commands_per_tick_budget;
    while (!pending_.empty() && budget > 0) {
        ApplyMove(pending_.front());
        pending_.pop_front();
        --budget;
    }
    stats_.commands_deferred += pending_.size();
}

void Zone::ApplyMove(const MoveCommand& command) {
    const auto it = players_.find(command.player);
    if (it == players_.end()) {
        return;  // player left while the command was queued
    }
    Player& player = it->second;
    // Inputs are ordered per connection; anything not newer than the last applied one is a
    // duplicate or a reordering and must not rewind intent (see README "네트워크와 입장").
    if (command.input_seq <= player.last_input_seq && player.last_input_seq != 0) {
        ++stats_.stale_inputs_dropped;
        return;
    }
    player.last_input_seq = command.input_seq;
    player.desired_direction = command.direction.Normalized();
    ++stats_.commands_applied;
}

// ---- simulation -----------------------------------------------------------------------------

void Zone::Tick() {
    tick_ = tick_.Next();
    DrainCommands();
    Integrate();
    UpdateInterestSets();
}

void Zone::Integrate() {
    for (auto& [id, player] : players_) {
        player.moved_this_tick = false;
        if (player.desired_direction.IsZero()) {
            player.velocity = {};
            continue;
        }
        player.velocity = player.desired_direction * rules_.max_speed_mps;
        const common::Vec2 target = player.position + player.velocity * dt_seconds_;
        const common::Vec2 resolved = ResolveCollisions(player.position, target);
        if (resolved != player.position) {
            player.position = resolved;
            player.moved_this_tick = true;
            grid_.Move(id, resolved);
        } else {
            player.velocity = {};
        }
    }
}

bool Zone::Blocked(common::Vec2 point) const {
    if (point.x < kPlayerRadiusM || point.y < kPlayerRadiusM ||
        point.x > map_.size.x - kPlayerRadiusM || point.y > map_.size.y - kPlayerRadiusM) {
        return true;
    }
    const common::Vec2 footprint{kPlayerRadiusM, kPlayerRadiusM};
    return std::ranges::any_of(map_.obstacles, [&](const common::Aabb& box) {
        const common::Aabb grown{.min = box.min - footprint, .max = box.max + footprint};
        return grown.Contains(point);
    });
}

// Axis-separated resolution: try the full move, then each axis alone so the player slides along
// walls instead of sticking to them. Movement per tick is far smaller than any obstacle, so
// tunnelling is not a concern at the configured speeds.
common::Vec2 Zone::ResolveCollisions(common::Vec2 from, common::Vec2 to) const {
    if (!Blocked(to)) {
        return to;
    }
    const common::Vec2 x_only{to.x, from.y};
    if (!Blocked(x_only)) {
        return x_only;
    }
    const common::Vec2 y_only{from.x, to.y};
    if (!Blocked(y_only)) {
        return y_only;
    }
    return from;
}

// ---- interest management ---------------------------------------------------------------------

void Zone::UpdateInterestSets() {
    for (auto& [id, player] : players_) {
        RecomputeInterest(id, player);
    }
}

void Zone::RecomputeInterest(common::EntityId self, Player& player) {
    scratch_.clear();
    grid_.QueryCandidates(player.position, rules_.aoi_leave_radius_m, scratch_);

    const float enter_sq = rules_.aoi_enter_radius_m * rules_.aoi_enter_radius_m;
    const float leave_sq = rules_.aoi_leave_radius_m * rules_.aoi_leave_radius_m;

    std::vector<common::EntityId> next;
    next.reserve(player.interest.size() + 4);
    for (const common::EntityId candidate : scratch_) {
        if (candidate == self) {
            continue;
        }
        const auto other = players_.find(candidate);
        if (other == players_.end()) {
            continue;
        }
        const float d_sq = common::DistanceSq(player.position, other->second.position);
        const bool was_visible = SortedContains(player.interest, candidate);
        if (d_sq <= enter_sq || (was_visible && d_sq <= leave_sq)) {
            next.push_back(candidate);
        }
    }
    std::ranges::sort(next);
    player.interest = std::move(next);
}

// ---- output -----------------------------------------------------------------------------------

EntitySnapshot Zone::MakeSnapshot(common::EntityId id, const Player& player) {
    return EntitySnapshot{.id = id,
                          .kind = EntityKind::kPlayer,
                          .template_id = player.class_id,
                          .position = player.position,
                          .velocity = player.velocity};
}

PlayerView Zone::CollectView(common::EntityId player_id) {
    PlayerView view{.player = player_id};
    const auto it = players_.find(player_id);
    if (it == players_.end()) {
        return view;
    }
    Player& player = it->second;
    view.last_processed_input_seq = player.last_input_seq;

    // interest and known are both sorted: despawned = known \ interest in one merge pass.
    std::ranges::set_difference(player.known, player.interest, std::back_inserter(view.despawned));

    for (const common::EntityId id : player.interest) {
        const auto other = players_.find(id);
        if (other == players_.end()) {
            continue;
        }
        if (!SortedContains(player.known, id)) {
            view.spawned.push_back(MakeSnapshot(id, other->second));
        } else if (other->second.moved_this_tick) {
            view.updated.push_back(MakeSnapshot(id, other->second));
        }
    }
    player.known = player.interest;
    return view;
}

// ---- inspection -------------------------------------------------------------------------------

std::optional<common::Vec2> Zone::PositionOf(common::EntityId player) const {
    const auto it = players_.find(player);
    return it == players_.end() ? std::nullopt : std::optional{it->second.position};
}

std::optional<EntitySnapshot> Zone::SnapshotOf(common::EntityId entity) const {
    const auto it = players_.find(entity);
    return it == players_.end() ? std::nullopt : std::optional{MakeSnapshot(entity, it->second)};
}

}  // namespace sa::zone::sim
