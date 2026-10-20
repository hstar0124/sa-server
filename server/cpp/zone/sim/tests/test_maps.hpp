#pragma once

#include "sa/zone/sim/content.hpp"
#include "sa/zone/sim/map_data.hpp"
#include "sa/zone/sim/zone.hpp"

#include <chrono>
#include <stdexcept>

// Fixtures shared by the simulation tests. Numbers here are test geometry, not game tuning.
namespace sa::zone::sim::test {

using namespace std::chrono_literals;

constexpr ClassId kWarrior{1};
constexpr ClassId kPauper{2};     // 10 MP: cannot afford heavy_blow
constexpr SkillId kStrike{1};     // targeted, instant, 2.5 m, 20 dmg
constexpr SkillId kHeavyBlow{2};  // targeted, 600 ms cast, 15 mp, 60 dmg
constexpr SkillId kArrow{3};      // projectile, instant, 5 mp, 25 dmg, 30 m
constexpr SkillId kUnlearned{4};  // exists, not on the warrior
constexpr MonsterTemplateId kSlime{101};
constexpr ItemTemplateId kGel{2001};    // always dropped by the slime
constexpr ItemTemplateId kSword{2101};  // never dropped (chance 0)

inline ContentBundle Content() {
    ContentBundle c;
    c.version = "test";
    c.skills[kStrike] = SkillDefinition{.id = kStrike,
                                        .name = "strike",
                                        .kind = SkillKind::kTargeted,
                                        .cost_mp = 0,
                                        .cooldown = 800ms,
                                        .cast_time = 0ms,
                                        .range_m = 2.5F,
                                        .damage = 20};
    c.skills[kHeavyBlow] = SkillDefinition{.id = kHeavyBlow,
                                           .name = "heavy",
                                           .kind = SkillKind::kTargeted,
                                           .cost_mp = 15,
                                           .cooldown = 4000ms,
                                           .cast_time = 600ms,
                                           .range_m = 2.5F,
                                           .damage = 60};
    c.skills[kArrow] = SkillDefinition{.id = kArrow,
                                       .name = "arrow",
                                       .kind = SkillKind::kProjectile,
                                       .cost_mp = 5,
                                       .cooldown = 1000ms,
                                       .cast_time = 0ms,
                                       .range_m = 30.0F,
                                       .damage = 25,
                                       .projectile_speed_mps = 25.0F,
                                       .projectile_radius_m = 0.6F};
    c.skills[kUnlearned] = SkillDefinition{.id = kUnlearned,
                                           .name = "fireball",
                                           .kind = SkillKind::kProjectile,
                                           .cost_mp = 1,
                                           .cooldown = 1ms,
                                           .cast_time = 0ms,
                                           .range_m = 10.0F,
                                           .damage = 1,
                                           .projectile_speed_mps = 1.0F,
                                           .projectile_radius_m = 1.0F};
    c.classes[kPauper] = ClassDefinition{.id = kPauper,
                                         .name = "pauper",
                                         .max_hp = 100,
                                         .max_mp = 10,
                                         .mp_regen_per_sec = 0.0F,
                                         .skills = {kStrike, kHeavyBlow}};
    c.classes[kWarrior] = ClassDefinition{.id = kWarrior,
                                          .name = "warrior",
                                          .max_hp = 200,
                                          .max_mp = 50,
                                          .mp_regen_per_sec = 10.0F,
                                          .skills = {kStrike, kHeavyBlow, kArrow}};
    c.items[kGel] = ItemDefinition{.id = kGel, .name = "gel"};
    c.items[kSword] = ItemDefinition{.id = kSword, .name = "sword"};
    c.drops[kSlime] = DropTable{.monster = kSlime,
                                .gold = 3,
                                .entries = {DropEntry{.item = kGel, .chance = 1.0F},
                                            DropEntry{.item = kSword, .chance = 0.0F}}};
    c.monsters[kSlime] = MonsterDefinition{.id = kSlime,
                                           .name = "slime",
                                           .max_hp = 60,
                                           .speed_mps = 3.0F,
                                           .aggro_radius_m = 8.0F,
                                           .leash_radius_m = 25.0F,
                                           .attack_range_m = 1.5F,
                                           .attack_damage = 8,
                                           .attack_cooldown = 1000ms,
                                           .respawn = 2000ms};
    return c;
}

inline MapData OpenField() {
    return MapData{.size = {100.0F, 100.0F},
                   .cell_size = 10.0F,
                   .obstacles = {},
                   .spawn_points = {{10.0F, 10.0F}, {90.0F, 90.0F}},
                   .monster_spawns = {}};
}

inline MapData FieldWithWall() {
    MapData map = OpenField();
    map.obstacles.push_back(common::Aabb{.min = {50.0F, 0.0F}, .max = {52.0F, 60.0F}});
    return map;
}

inline MapData FieldWithSlime(common::Vec2 at = {70.0F, 70.0F}) {
    MapData map = OpenField();
    map.monster_spawns.push_back(MonsterSpawn{.template_id = kSlime, .position = at, .count = 1});
    return map;
}

inline MovementRules Rules() {
    return MovementRules{.max_speed_mps = 10.0F,
                         .body_radius_m = 0.4F,
                         .aoi_enter_radius_m = 20.0F,
                         .aoi_leave_radius_m = 25.0F,
                         .commands_per_tick_budget = 1024,
                         .max_queued_per_player = 8};
}

inline CombatRules Combat() {
    return CombatRules{.monster_think_hz = 5,
                       .player_respawn = 1000ms,
                       .monster_stall_timeout = 1000ms,
                       .monster_stall_distance_m = 1.0F,
                       .range_tolerance = 1.1F,
                       .monster_home_arrive_m = 0.5F,
                       .monster_pack_ring_radius_m = 1.5F};
}

inline EconomyRules Economy() {
    return EconomyRules{.inventory_capacity = 20,
                        .pickup_range_m = 2.0F,
                        .ground_item_despawn = 3000ms,
                        .primary_currency = "gold"};
}

inline ZoneRules AllRules(MovementRules movement = Rules()) {
    return ZoneRules{.movement = movement, .combat = Combat(), .economy = Economy()};
}

// Small on purpose, so tests can push results out of the retry window with a handful of requests.
inline constexpr std::size_t kTestRetryWindow = 4;

inline ZoneConfig Config(std::uint32_t hz = 10) {
    return ZoneConfig{.world_id = common::WorldId{1},
                      .zone_id = common::ZoneId{1},
                      .simulation_rate = TickRate{.hz = hz},
                      .command_retry_window = kTestRetryWindow};
}

inline Zone MakeZone(MapData map = OpenField(), MovementRules rules = Rules(),
                     std::uint64_t seed = 1) {
    auto zone = Zone::Create(Config(), std::move(map), AllRules(rules), Content(), Rng{seed});
    if (!zone) {
        throw std::logic_error{"test zone could not be created: " + zone.error()};
    }
    return std::move(*zone);
}

// Spawns a player of the given class; a failure is a fixture bug.
inline common::EntityId Spawn(Zone& zone, std::uint64_t character, common::Vec2 at,
                              ClassId cls = kWarrior) {
    const auto id = zone.SpawnPlayer(common::CharacterId{character}, cls, at);
    if (!id) {
        throw std::logic_error{"spawn failed"};
    }
    return *id;
}

inline common::EntityId SpawnAnywhere(Zone& zone, std::uint64_t character) {
    const auto id = zone.SpawnPlayer(common::CharacterId{character}, kWarrior);
    if (!id) {
        throw std::logic_error{"spawn failed"};
    }
    return *id;
}

// Position of a player that the test knows to exist; a missing player is a test bug, not UB.
inline common::Vec2 PosOf(const Zone& zone, common::EntityId player) {
    const auto position = zone.PositionOf(player);
    if (!position.has_value()) {
        throw std::logic_error{"player is not in the zone"};
    }
    return *position;
}

inline EntitySnapshot SnapshotOf(const Zone& zone, common::EntityId entity) {
    const auto snapshot = zone.SnapshotOf(entity);
    if (!snapshot.has_value()) {
        throw std::logic_error{"entity is not in the zone"};
    }
    return *snapshot;
}

inline Vitals VitalsOf(const Zone& zone, common::EntityId entity) {
    const auto vitals = zone.VitalsOf(entity);
    if (!vitals.has_value()) {
        throw std::logic_error{"entity has no vitals"};
    }
    return *vitals;
}

inline common::EntityId OnlyMonster(const Zone& zone) {
    const auto monsters = zone.LiveMonsters();
    if (monsters.size() != 1) {
        throw std::logic_error{"expected exactly one live monster"};
    }
    return monsters.front();
}

inline void TickN(Zone& zone, int n) {
    for (int i = 0; i < n; ++i) {
        zone.Tick();
    }
}

}  // namespace sa::zone::sim::test
