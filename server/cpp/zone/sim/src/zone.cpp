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

bool SortedContains(const std::vector<common::EntityId>& sorted, common::EntityId id) {
    return std::ranges::binary_search(sorted, id);
}

// Adds the time since the previous lap to a phase's total. Without a clock it does nothing, so a
// zone in tests or tools pays no clock reads.
class PhaseTimer {
public:
    explicit PhaseTimer(const Zone::ProfileClock& clock)
        : clock_(clock), last_(clock ? clock() : std::chrono::steady_clock::time_point{}) {}

    void Lap(std::chrono::nanoseconds& total) {
        if (!clock_) {
            return;
        }
        const auto now = clock_();
        total += now - last_;
        last_ = now;
    }

private:
    const Zone::ProfileClock& clock_;
    std::chrono::steady_clock::time_point last_;
};

}  // namespace

// ---- construction -------------------------------------------------------------------------------

std::expected<Zone, std::string> Zone::Create(ZoneConfig config, MapData map, ZoneRules rules,
                                              ContentBundle content, Rng rng) {
    if (config.command_retry_window == 0) {
        return std::unexpected("command_retry_window must be positive");
    }
    if (config.ticks_per_view == 0) {
        return std::unexpected("ticks_per_view must be positive");
    }
    if (auto valid = map.ValidateAgainst(content); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto walkable = map.ValidateWalkable(rules.movement.body_radius_m,
                                             rules.combat.monster_pack_ring_radius_m);
        !walkable) {
        return std::unexpected(walkable.error());
    }
    Zone zone{config, std::move(map), std::move(rules), std::move(content), rng};
    if (auto portals = zone.ValidatePortalsLeaveZone(); !portals) {
        return std::unexpected(portals.error());
    }
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

// ---- membership ---------------------------------------------------------------------------------

std::expected<common::EntityId, SpawnError> Zone::SpawnPlayer(
    common::CharacterId character, ClassId class_id, std::optional<common::Vec2> position) {
    const auto cls = content_.classes.find(class_id);
    if (cls == content_.classes.end()) {
        return std::unexpected(SpawnError::kUnknownClass);
    }
    const bool fits = position && map_.IsWalkable(*position, rules_.movement.body_radius_m);
    if (position && !fits) {
        ++stats_.spawns_relocated;
    }
    common::Vec2 at = position.value_or(common::Vec2{});
    if (!fits) {
        const auto index = rng_.Range<std::size_t>(0, map_.spawn_points.size() - 1);
        at = map_.spawn_points[index];
    }
    const common::EntityId id = AllocateEntityId();
    Player player{.character = character, .cls = &cls->second, .position = at};
    player.published = Published{.position = at, .velocity = {}, .at = tick_};
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
    // Observers learn about the departure through their next view.
}

// ---- input --------------------------------------------------------------------------------------

void Zone::Submit(Command command) {
    std::visit(
        [this](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, MoveCommand>) {
                QueueMove(c);
            } else {
                QueueRequest(c.player, c.request_id, c);
            }
        },
        command);
}

void Zone::QueueMove(const MoveCommand& move) {
    const auto [it, inserted] = pending_moves_.try_emplace(move.player, move);
    if (inserted) {
        return;
    }
    if (move.input_seq > it->second.input_seq) {
        it->second = move;
        ++stats_.moves_superseded;
    } else {
        ++stats_.stale_inputs_dropped;  // reordered behind a newer move that is already waiting
    }
}

void Zone::QueueRequest(common::EntityId player, common::RequestId request, QueuedCommand command) {
    std::uint32_t& queued = queued_per_player_[player];
    if (queued >= rules_.movement.max_queued_per_player) {
        ++stats_.commands_dropped;
        Deliver(player, CommandResultEvent::Unknown(request));
        return;
    }
    ++queued;
    pending_.push_back(command);
}

void Zone::DrainCommands() {
    // At most one move per player is waiting, so moves need no budget.
    for (const auto& [player, move] : pending_moves_) {
        ApplyMove(move);
    }
    pending_moves_.clear();

    std::size_t applied = 0;
    while (!pending_.empty() && applied < rules_.movement.commands_per_tick_budget) {
        const common::EntityId player = std::visit(
            [this](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, CastCommand>) {
                    ApplyCast(c);
                } else if constexpr (std::is_same_v<T, PickupCommand>) {
                    ApplyPickup(c);
                } else {
                    ApplyChangeZone(c);
                }
                return c.player;
            },
            pending_.front());
        pending_.pop_front();
        ++applied;
        if (const auto queued = queued_per_player_.find(player);
            queued != queued_per_player_.end() && --queued->second == 0) {
            queued_per_player_.erase(queued);
        }
    }
    // Each waiting command counts once. The queue is FIFO, so the ones counted on earlier ticks
    // were at its front and were applied first.
    const std::size_t counted = deferred_in_queue_ > applied ? deferred_in_queue_ - applied : 0;
    stats_.commands_deferred += pending_.size() - counted;
    deferred_in_queue_ = pending_.size();
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
    // The dead and the departing have no intent: an input sent then must not move the body.
    player.desired_direction = player.vitals.IsAlive() && !player.departing
                                   ? command.direction.Normalized()
                                   : common::Vec2{};
    ++stats_.commands_applied;
}

// ---- simulation ---------------------------------------------------------------------------------

void Zone::Tick() {
    tick_ = tick_.Next();
    PhaseTimer phase{profile_clock_};
    DrainCommands();  // ValidateAndApplyCommands within the per-tick budget
    phase.Lap(profile_.commands);
    ResolveCasts();  // casts whose cast time elapsed
    phase.Lap(profile_.casts);
    IntegratePlayers();  // UpdateMovementAndCollision
    phase.Lap(profile_.players);
    UpdateMonsters();  // UpdateScheduledAi
    phase.Lap(profile_.monsters);
    UpdateProjectiles();  // UpdateCastsAndProjectiles
    phase.Lap(profile_.projectiles);
    RegenerateMp();
    RespawnPlayers();  // ApplyDamageBuffsAndDeaths: deferred effects
    RespawnMonsters();
    ExpireGroundItems();
    phase.Lap(profile_.upkeep);
    UpdateInterestSets();  // UpdateInterestSets
    phase.Lap(profile_.interest);
    stats_.monsters = monsters_.size();
    stats_.projectiles = projectiles_.size();
}

// Every player passes the dead-reckoning check each tick, standing ones included: a respawn moves
// a body without walking it, and only the check notices.
void Zone::IntegratePlayers() {
    for (auto& [id, player] : players_) {
        // Casting roots the caster; the intent is kept and applied once the cast resolves.
        const bool rooted = player.cast.has_value() || !player.vitals.IsAlive();
        player.velocity = {};
        if (!rooted && !player.desired_direction.IsZero()) {
            const common::Vec2 intended = player.desired_direction * rules_.movement.max_speed_mps;
            const common::Vec2 resolved =
                ResolveCollisions(player.position, player.position + intended * dt_seconds_);
            // The achieved velocity, not the intended one: sliding along a wall is slower, and an
            // observer extrapolating with it must land where the zone did.
            player.velocity = (resolved - player.position) * (1.0F / dt_seconds_);
            if (resolved != player.position) {
                player.position = resolved;
                grid_.Move(id, resolved);
            }
        }
        PublishIfDeviated(player.published, player.position, player.velocity, player.changed_at);
    }
}

void Zone::PublishIfDeviated(Published& published, common::Vec2 position, common::Vec2 velocity,
                             common::TickIndex& changed_at) const {
    const MovementRules& rules = rules_.movement;
    const float elapsed_s = static_cast<float>(tick_.value() - published.at.value()) * dt_seconds_;
    const common::Vec2 extrapolated = published.position + published.velocity * elapsed_s;
    const bool turned = common::DistanceSq(velocity, published.velocity) >
                        rules.publish_velocity_delta_mps * rules.publish_velocity_delta_mps;
    const bool strayed = common::DistanceSq(position, extrapolated) >
                         rules.publish_position_error_m * rules.publish_position_error_m;
    if (turned || strayed) {
        Publish(published, position, velocity, changed_at);
    }
}

void Zone::Publish(Published& published, common::Vec2 position, common::Vec2 velocity,
                   common::TickIndex& changed_at) const {
    published = Published{.position = position, .velocity = velocity, .at = tick_};
    changed_at = tick_;
}

bool Zone::Blocked(common::Vec2 point) const {
    return !map_.IsWalkable(point, rules_.movement.body_radius_m);
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

// ---- interest management ------------------------------------------------------------------------

// Only on view ticks: views are what read interest sets, and the recomputation is most of a tick's
// cost in a crowd.
void Zone::UpdateInterestSets() {
    if (tick_.value() % config_.ticks_per_view != 0) {
        return;
    }
    for (auto& [id, player] : players_) {
        RecomputeInterest(id, player);
    }
}

void Zone::RecomputeInterest(common::EntityId self, Player& player) {
    scratch_.clear();
    grid_.QueryCandidates(player.position, rules_.movement.aoi_leave_radius_m, scratch_);

    const float enter_sq = rules_.movement.aoi_enter_radius_m * rules_.movement.aoi_enter_radius_m;
    const float leave_sq = rules_.movement.aoi_leave_radius_m * rules_.movement.aoi_leave_radius_m;

    // Candidates in range, each with the distance it is ranked by. An entity already visible is
    // ranked one hysteresis band closer, so at the view cap a newcomer has to be clearly nearer to
    // push it out, and the edge of a crowd does not trade places every tick.
    const float band = rules_.movement.aoi_leave_radius_m - rules_.movement.aoi_enter_radius_m;
    ranked_.clear();
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
            const float d = std::sqrt(d_sq);
            ranked_.push_back(Ranked{.id = candidate, .rank = was_visible ? d - band : d});
        }
    }
    KeepNearest(ranked_, rules_.movement.aoi_max_entities);
    player.interest.clear();
    for (const Ranked& r : ranked_) {
        player.interest.push_back(r.id);
    }
    std::ranges::sort(player.interest);
}

void Zone::KeepNearest(std::vector<Ranked>& ranked, std::size_t limit) {
    if (ranked.size() <= limit) {
        return;
    }
    // Ties go to the lower id, so two zones with the same state keep the same entities.
    const auto by_rank = [](const Ranked& a, const Ranked& b) {
        return a.rank < b.rank || (a.rank == b.rank && a.id < b.id);
    };
    std::ranges::nth_element(ranked, ranked.begin() + static_cast<std::ptrdiff_t>(limit), by_rank);
    ranked.resize(limit);
}

// ---- output -------------------------------------------------------------------------------------

std::optional<common::TickIndex> Zone::ChangedAt(common::EntityId entity) const {
    if (const auto p = players_.find(entity); p != players_.end()) {
        return p->second.changed_at;
    }
    if (const auto m = monsters_.find(entity); m != monsters_.end()) {
        return m->second.changed_at;
    }
    if (const auto pr = projectiles_.find(entity); pr != projectiles_.end()) {
        return pr->second.changed_at;  // a straight flight: reported once, at launch
    }
    if (items_.contains(entity)) {
        return common::TickIndex{};  // items never change
    }
    return std::nullopt;
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

    // The interest set was computed on a view tick, so an entity that left the zone since is
    // skipped here; it drops out of `known` and is reported despawned below with the rest.
    ranked_.clear();
    std::vector<common::EntityId> next_known;
    next_known.reserve(player.interest.size());
    for (const common::EntityId id : player.interest) {
        if (!SortedContains(player.known, id)) {
            if (const auto position = PositionOf(id)) {
                ranked_.push_back(
                    Ranked{.id = id, .rank = common::Distance(player.position, *position)});
            }
            continue;
        }
        const auto changed_at = ChangedAt(id);
        if (!changed_at) {
            continue;
        }
        next_known.push_back(id);
        if (*changed_at > player.viewed_at) {
            if (auto snapshot = SnapshotOf(id)) {
                view.updated.push_back(*snapshot);
            }
        }
    }
    // Newcomers beyond the per-view limit stay unknown and are offered again next time, nearest
    // first; they are in the interest set, so they cannot be missed.
    KeepNearest(ranked_, rules_.movement.aoi_max_spawns_per_view);
    for (const Ranked& r : ranked_) {
        if (auto snapshot = SnapshotOf(r.id)) {
            view.spawned.push_back(*snapshot);
            next_known.push_back(r.id);
        }
    }
    std::ranges::sort(next_known);
    // Both sorted: despawned = what the client knew and no longer keeps, in one merge pass.
    std::ranges::set_difference(player.known, next_known, std::back_inserter(view.despawned));
    player.known = std::move(next_known);
    player.viewed_at = tick_;
    return view;
}

std::vector<DeliveredEvent> Zone::DrainEvents() {
    return std::exchange(outbox_, {});
}

std::vector<HostRequest> Zone::DrainHostRequests() {
    return std::exchange(host_requests_, {});
}

bool Zone::Knows(const Player& player, common::EntityId entity) {
    return SortedContains(player.known, entity);
}

void Zone::Deliver(common::EntityId recipient, ZoneEvent event) {
    outbox_.push_back(DeliveredEvent{.recipient = recipient, .event = event});
}

void Zone::DeliverToObservers(common::EntityId subject, const ZoneEvent& event) {
    for (const auto& [id, player] : players_) {
        if (id == subject || Knows(player, subject)) {
            Deliver(id, event);
        }
    }
}

// ---- inspection ---------------------------------------------------------------------------------

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

// ---- recent requests ----------------------------------------------------------------------------

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
