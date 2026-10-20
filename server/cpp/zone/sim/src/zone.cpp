#include "sa/zone/sim/zone.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <span>
#include <type_traits>
#include <utility>

namespace sa::zone::sim {
namespace {

// Half-width of a player's or monster's footprint for terrain tests; a property of the collision
// model, not of the content.
constexpr float kBodyRadiusM = 0.4F;

bool SortedContains(const std::vector<common::EntityId>& sorted, common::EntityId id) {
    return std::ranges::binary_search(sorted, id);
}

}  // namespace

// ---- construction -------------------------------------------------------------------------------

std::expected<Zone, std::string> Zone::Create(ZoneConfig config, MapData map, ZoneRules rules,
                                              ContentBundle content, Rng rng) {
    if (config.command_retry_window == 0) {
        return std::unexpected("command_retry_window must be positive");
    }
    if (auto valid = map.ValidateAgainst(content); !valid) {
        return std::unexpected(valid.error());
    }
    Zone zone{config, std::move(map), std::move(rules), std::move(content), rng};
    zone.PopulateMonsterSlots();
    return zone;
}

Zone::Zone(ZoneConfig config, MapData map, ZoneRules rules, ContentBundle content, Rng rng)
    : config_(config),
      map_(std::move(map)),
      rules_(std::move(rules)),
      content_(std::move(content)),
      rng_(rng),
      dt_seconds_(std::chrono::duration<float>(config.simulation_rate.Period()).count()),
      grid_(map_.size, map_.cell_size) {}

common::EntityId Zone::AllocateEntityId() {
    const common::EntityId id = next_entity_id_;
    next_entity_id_ = next_entity_id_.Next();
    return id;
}

common::TickIndex Zone::TicksFromNow(std::chrono::milliseconds duration) const {
    const auto period = config_.simulation_rate.Period();
    const auto ticks =
        (std::chrono::duration_cast<Duration>(duration) + period - Duration{1}) / period;
    return common::TickIndex{tick_.value() +
                             static_cast<std::uint64_t>(std::max<std::int64_t>(0, ticks))};
}

// ---- membership
// -----------------------------------------------------------------------------------

std::optional<common::EntityId> Zone::SpawnPlayer(common::CharacterId character, ClassId class_id,
                                                  std::optional<common::Vec2> position) {
    const auto cls = content_.classes.find(class_id);
    if (cls == content_.classes.end()) {
        return std::nullopt;
    }
    common::Vec2 at = position.value_or(common::Vec2{});
    if (!position) {
        const auto index = rng_.Range<std::size_t>(0, map_.spawn_points.size() - 1);
        at = map_.spawn_points[index];
    }
    const common::EntityId id = AllocateEntityId();
    Player player{.character = character, .cls = &cls->second, .position = at};
    player.recent_requests = RecentRequests{config_.command_retry_window};
    player.vitals = Vitals{.hp = cls->second.max_hp,
                           .max_hp = cls->second.max_hp,
                           .mp = cls->second.max_mp,
                           .max_mp = cls->second.max_mp};
    players_.emplace(id, std::move(player));
    kinds_.emplace(id, EntityKind::kPlayer);
    grid_.Insert(id, at);
    stats_.players = players_.size();
    return id;
}

void Zone::DespawnPlayer(common::EntityId player) {
    if (players_.erase(player) == 0) {
        return;
    }
    kinds_.erase(player);
    grid_.Remove(player);
    stats_.players = players_.size();
    // Observers learn about the departure through their next interest recomputation.
}

// ---- input
// --------------------------------------------------------------------------------------------

void Zone::Submit(Command command) {
    pending_.push_back(command);
}

void Zone::DrainCommands() {
    std::uint32_t budget = rules_.movement.commands_per_tick_budget;
    while (!pending_.empty() && budget > 0) {
        std::visit(
            [this](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, MoveCommand>) {
                    ApplyMove(c);
                } else if constexpr (std::is_same_v<T, CastCommand>) {
                    ApplyCast(c);
                } else {
                    ApplyPickup(c);
                }
            },
            pending_.front());
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
    // The dead have no intent: an input sent while dead must not make the respawned body walk.
    player.desired_direction =
        player.vitals.IsAlive() ? command.direction.Normalized() : common::Vec2{};
    ++stats_.commands_applied;
}

// ---- simulation
// ---------------------------------------------------------------------------------------

void Zone::Tick() {
    tick_ = tick_.Next();
    DrainCommands();      // ValidateAndApplyCommands within the per-tick budget
    ResolveCasts();       // casts whose cast time elapsed
    IntegratePlayers();   // UpdateMovementAndCollision
    UpdateMonsters();     // UpdateScheduledAi
    UpdateProjectiles();  // UpdateCastsAndProjectiles
    RegenerateMp();
    RespawnPlayers();  // ApplyDamageBuffsAndDeaths: deferred effects
    RespawnMonsters();
    ExpireGroundItems();
    UpdateInterestSets();  // UpdateInterestSets
    stats_.monsters = monsters_.size();
    stats_.projectiles = projectiles_.size();
}

void Zone::IntegratePlayers() {
    for (auto& [id, player] : players_) {
        // Casting roots the caster; the intent is kept and applied once the cast resolves.
        const bool rooted = player.cast.has_value() || !player.vitals.IsAlive();
        if (rooted || player.desired_direction.IsZero()) {
            SetVelocity(player.velocity, player.changed_at, {});
            continue;
        }
        const common::Vec2 velocity = player.desired_direction * rules_.movement.max_speed_mps;
        const common::Vec2 resolved =
            ResolveCollisions(player.position, player.position + velocity * dt_seconds_);
        if (resolved == player.position) {
            SetVelocity(player.velocity, player.changed_at, {});
            continue;
        }
        SetVelocity(player.velocity, player.changed_at, velocity);
        player.position = resolved;
        player.changed_at = tick_;
        grid_.Move(id, resolved);
    }
}

void Zone::SetVelocity(common::Vec2& velocity, common::TickIndex& changed_at,
                       common::Vec2 next) const {
    if (velocity != next) {
        velocity = next;
        changed_at = tick_;
    }
}

bool Zone::Blocked(common::Vec2 point) const {
    if (point.x < kBodyRadiusM || point.y < kBodyRadiusM || point.x > map_.size.x - kBodyRadiusM ||
        point.y > map_.size.y - kBodyRadiusM) {
        return true;
    }
    const common::Vec2 footprint{kBodyRadiusM, kBodyRadiusM};
    return std::ranges::any_of(map_.obstacles, [&](const common::Aabb& box) {
        const common::Aabb grown{.min = box.min - footprint, .max = box.max + footprint};
        return grown.Contains(point);
    });
}

// Axis-separated resolution: try the full move, then each axis alone so bodies slide along walls
// instead of sticking. Movement per tick is far smaller than any obstacle, so tunnelling is not a
// concern at the configured speeds.
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

// One tick of travel from `from` toward `goal`, never overshooting it.
common::Vec2 Zone::StepTowards(common::Vec2 from, common::Vec2 goal, float speed_mps) const {
    const common::Vec2 delta = goal - from;
    const float distance = delta.Length();
    const float step = speed_mps * dt_seconds_;
    if (distance <= step) {
        return ResolveCollisions(from, goal);
    }
    return ResolveCollisions(from, from + delta.Normalized() * step);
}

// ---- interest management
// ---------------------------------------------------------------------------

void Zone::UpdateInterestSets() {
    for (auto& [id, player] : players_) {
        RecomputeInterest(id, player);
    }
}

void Zone::RecomputeInterest(common::EntityId self, Player& player) {
    scratch_.clear();
    grid_.QueryCandidates(player.position, rules_.movement.aoi_leave_radius_m, scratch_);

    const float enter_sq = rules_.movement.aoi_enter_radius_m * rules_.movement.aoi_enter_radius_m;
    const float leave_sq = rules_.movement.aoi_leave_radius_m * rules_.movement.aoi_leave_radius_m;

    std::vector<common::EntityId> next;
    next.reserve(player.interest.size() + 4);
    for (const common::EntityId candidate : scratch_) {
        if (candidate == self) {
            continue;
        }
        const auto position = PositionOf(candidate);
        if (!position) {
            continue;
        }
        const float d_sq = common::DistanceSq(player.position, *position);
        const bool was_visible = SortedContains(player.interest, candidate);
        if (d_sq <= enter_sq || (was_visible && d_sq <= leave_sq)) {
            next.push_back(candidate);
        }
    }
    std::ranges::sort(next);
    player.interest = std::move(next);
}

// ---- output
// -----------------------------------------------------------------------------------------

bool Zone::ChangedSince(common::EntityId entity, common::TickIndex tick) const {
    if (const auto p = players_.find(entity); p != players_.end()) {
        return p->second.changed_at > tick;
    }
    if (const auto m = monsters_.find(entity); m != monsters_.end()) {
        return m->second.changed_at > tick;
    }
    return projectiles_.contains(entity);  // projectiles move every tick; items never change
}

PlayerView Zone::CollectView(common::EntityId player_id) {
    PlayerView view{.player = player_id};
    const auto it = players_.find(player_id);
    if (it == players_.end()) {
        return view;
    }
    Player& player = it->second;
    view.last_processed_input_seq = player.last_input_seq;
    view.self = SnapshotOf(player_id).value_or(EntitySnapshot{});
    view.self_vitals = player.vitals;

    // interest and known are both sorted: despawned = known \ interest in one merge pass.
    std::ranges::set_difference(player.known, player.interest, std::back_inserter(view.despawned));
    for (const common::EntityId id : player.interest) {
        if (!SortedContains(player.known, id)) {
            if (auto snapshot = SnapshotOf(id)) {
                view.spawned.push_back(*snapshot);
            }
        } else if (ChangedSince(id, player.viewed_at)) {
            if (auto snapshot = SnapshotOf(id)) {
                view.updated.push_back(*snapshot);
            }
        }
    }
    player.known = player.interest;
    player.viewed_at = tick_;
    return view;
}

std::vector<DeliveredEvent> Zone::DrainEvents() {
    return std::exchange(outbox_, {});
}

std::vector<HostRequest> Zone::DrainHostRequests() {
    return std::exchange(host_requests_, {});
}

bool Zone::SortedContainsInterest(const Player& player, common::EntityId entity) {
    return SortedContains(player.interest, entity);
}

void Zone::Deliver(common::EntityId recipient, ZoneEvent event) {
    outbox_.push_back(DeliveredEvent{.recipient = recipient, .event = event});
}

void Zone::DeliverToObservers(common::EntityId subject, const ZoneEvent& event) {
    for (const auto& [id, player] : players_) {
        if (id == subject || SortedContains(player.interest, subject)) {
            Deliver(id, event);
        }
    }
}

// ---- inspection
// ----------------------------------------------------------------------------------------

std::optional<common::Vec2> Zone::PositionOf(common::EntityId entity) const {
    if (const auto p = players_.find(entity); p != players_.end()) {
        return p->second.position;
    }
    if (const auto m = monsters_.find(entity); m != monsters_.end()) {
        return m->second.position;
    }
    if (const auto pr = projectiles_.find(entity); pr != projectiles_.end()) {
        return pr->second.position;
    }
    if (const auto item = items_.find(entity); item != items_.end()) {
        return item->second.position;
    }
    return std::nullopt;
}

std::optional<EntitySnapshot> Zone::SnapshotOf(common::EntityId entity) const {
    if (const auto p = players_.find(entity); p != players_.end()) {
        const Player& player = p->second;
        return EntitySnapshot{.id = entity,
                              .kind = EntityKind::kPlayer,
                              .template_id = player.cls->id.value(),
                              .position = player.position,
                              .velocity = player.velocity,
                              .hp = player.vitals.hp,
                              .max_hp = player.vitals.max_hp};
    }
    if (const auto m = monsters_.find(entity); m != monsters_.end()) {
        const Monster& monster = m->second;
        return EntitySnapshot{.id = entity,
                              .kind = EntityKind::kMonster,
                              .template_id = monster.def->id.value(),
                              .position = monster.position,
                              .velocity = monster.velocity,
                              .hp = monster.vitals.hp,
                              .max_hp = monster.vitals.max_hp};
    }
    if (const auto pr = projectiles_.find(entity); pr != projectiles_.end()) {
        const Projectile& projectile = pr->second;
        return EntitySnapshot{.id = entity,
                              .kind = EntityKind::kProjectile,
                              .template_id = projectile.skill->id.value(),
                              .position = projectile.position,
                              .velocity = projectile.velocity};
    }
    if (const auto item = items_.find(entity); item != items_.end()) {
        return EntitySnapshot{.id = entity,
                              .kind = EntityKind::kDroppedItem,
                              .template_id = item->second.def->id.value(),
                              .position = item->second.position};
    }
    return std::nullopt;
}

std::optional<Vitals> Zone::VitalsOf(common::EntityId entity) const {
    if (const auto p = players_.find(entity); p != players_.end()) {
        return p->second.vitals;
    }
    if (const auto m = monsters_.find(entity); m != monsters_.end()) {
        return m->second.vitals;
    }
    return std::nullopt;
}

std::optional<EntityKind> Zone::KindOf(common::EntityId entity) const {
    const auto it = kinds_.find(entity);
    return it == kinds_.end() ? std::nullopt : std::optional{it->second};
}

std::vector<common::EntityId> Zone::LiveMonsters() const {
    std::vector<common::EntityId> out;
    out.reserve(monsters_.size());
    for (const auto& [id, _] : monsters_) {
        out.push_back(id);
    }
    std::ranges::sort(out);
    return out;
}

// ---- recent requests
// -----------------------------------------------------------------------------------

std::optional<CommandResultEvent> Zone::RecentRequests::Find(common::RequestId id) const {
    const auto filled = std::span{ring_}.first(size_);
    const auto it = std::ranges::find(filled, id, &CommandResultEvent::request_id);
    return it == filled.end() ? std::nullopt : std::optional{*it};
}

void Zone::RecentRequests::Remember(const CommandResultEvent& result) {
    if (ring_.empty()) {
        return;
    }
    const auto filled = std::span{ring_}.first(size_);
    if (const auto it =
            std::ranges::find(filled, result.request_id, &CommandResultEvent::request_id);
        it != filled.end()) {
        *it = result;
        return;
    }
    ring_.at(next_) = result;
    next_ = (next_ + 1) % ring_.size();
    size_ = std::min(size_ + 1, ring_.size());
}

}  // namespace sa::zone::sim
