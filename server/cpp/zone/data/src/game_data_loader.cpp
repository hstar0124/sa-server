#include "sa/zone/data/game_data_loader.hpp"

#include "toml_fields.hpp"

#include <toml++/toml.hpp>

#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace sa::zone::data {
namespace {

using namespace toml_fields;  // NOLINT(google-build-using-namespace): the loader's own field
                              // readers

// ---- map ----------------------------------------------------------------------------------------

std::expected<sim::MapData, LoadError> MapFromTable(const toml::table& table) {
    sim::MapData map;
    const auto width = RequireFloat(table, "map.width");
    const auto height = RequireFloat(table, "map.height");
    const auto cell = RequireFloat(table, "map.cell_size");
    if (auto err = FirstError(width, height, cell)) {
        return std::unexpected(*err);
    }
    map.size = {*width, *height};
    map.cell_size = *cell;

    auto obstacles = RequireTuples(table, "map.obstacles", 4);
    if (!obstacles) {
        return std::unexpected(obstacles.error());
    }
    for (const auto& box : *obstacles) {
        map.obstacles.push_back(common::Aabb{.min = {box[0], box[1]}, .max = {box[2], box[3]}});
    }
    auto spawns = RequireTuples(table, "map.spawn_points", 2);
    if (!spawns) {
        return std::unexpected(spawns.error());
    }
    for (const auto& point : *spawns) {
        map.spawn_points.emplace_back(point[0], point[1]);
    }
    auto monsters = ForEachTable(
        table, "monster_spawns", [&](const toml::table& t) -> std::expected<void, LoadError> {
            const auto id = RequireU32(t, "template_id");
            const auto position = RequirePair(t, "position");
            const auto count = RequireU32(t, "count");
            if (auto err = FirstError(id, position, count)) {
                return std::unexpected(LoadError{"monster_spawns: " + err->message});
            }
            map.monster_spawns.push_back(
                sim::MonsterSpawn{.template_id = sim::MonsterTemplateId{*id},
                                  .position = {(*position)[0], (*position)[1]},
                                  .count = *count});
            return {};
        });
    if (!monsters) {
        return std::unexpected(monsters.error());
    }
    auto portals =
        ForEachTable(table, "portals", [&](const toml::table& t) -> std::expected<void, LoadError> {
            const auto id = RequireU32(t, "id");
            const auto position = RequirePair(t, "position");
            const auto radius = RequireFloat(t, "radius_m");
            const auto to_zone = RequireU32(t, "to_zone_id");
            const auto arrival = RequirePair(t, "arrival");
            if (auto err = FirstError(id, position, radius, to_zone, arrival)) {
                return std::unexpected(LoadError{"portals: " + err->message});
            }
            map.portals.push_back(sim::Portal{.id = sim::PortalId{*id},
                                              .position = {(*position)[0], (*position)[1]},
                                              .radius_m = *radius,
                                              .to_zone = common::ZoneId{*to_zone},
                                              .arrival = {(*arrival)[0], (*arrival)[1]}});
            return {};
        });
    if (!portals) {
        return std::unexpected(portals.error());
    }
    if (auto valid = map.Validate(); !valid) {
        return std::unexpected(LoadError{"invalid map: " + valid.error()});
    }
    return map;
}

// ---- rules --------------------------------------------------------------------------------------

std::expected<sim::MovementRules, LoadError> RulesFromTable(const toml::table& table) {
    const auto speed = RequireFloat(table, "movement.max_speed_mps");
    const auto body = RequireFloat(table, "movement.body_radius_m");
    const auto enter = RequireFloat(table, "aoi.enter_radius_m");
    const auto leave = RequireFloat(table, "aoi.leave_radius_m");
    const auto max_entities = RequireU32(table, "aoi.max_entities");
    const auto max_spawns = RequireU32(table, "aoi.max_spawns_per_view");
    const auto velocity_delta = RequireFloat(table, "sync.publish_velocity_delta_mps");
    const auto position_error = RequireFloat(table, "sync.publish_position_error_m");
    const auto budget = RequireU32(table, "input.commands_per_tick_budget");
    const auto queued = RequireU32(table, "input.max_queued_per_player");
    if (auto err = FirstError(speed, body, enter, leave, max_entities, max_spawns, velocity_delta,
                              position_error, budget, queued)) {
        return std::unexpected(*err);
    }
    const sim::MovementRules rules{.max_speed_mps = *speed,
                                   .body_radius_m = *body,
                                   .aoi_enter_radius_m = *enter,
                                   .aoi_leave_radius_m = *leave,
                                   .aoi_max_entities = *max_entities,
                                   .aoi_max_spawns_per_view = *max_spawns,
                                   .publish_velocity_delta_mps = *velocity_delta,
                                   .publish_position_error_m = *position_error,
                                   .commands_per_tick_budget = *budget,
                                   .max_queued_per_player = *queued};
    if (auto valid = rules.Validate(); !valid) {
        return std::unexpected(LoadError{"invalid movement rules: " + valid.error()});
    }
    return rules;
}

std::expected<sim::CombatRules, LoadError> CombatFromTable(const toml::table& table) {
    const auto think = RequireU32(table, "combat.monster_think_hz");
    const auto respawn = RequireMs(table, "combat.player_respawn_ms");
    const auto stall_timeout = RequireMs(table, "combat.monster_stall_timeout_ms");
    const auto stall_distance = RequireFloat(table, "combat.monster_stall_distance_m");
    const auto tolerance = RequireFloat(table, "combat.range_tolerance");
    const auto arrive = RequireFloat(table, "combat.monster_home_arrive_m");
    const auto ring = RequireFloat(table, "combat.monster_pack_ring_radius_m");
    if (auto err =
            FirstError(think, respawn, stall_timeout, stall_distance, tolerance, arrive, ring)) {
        return std::unexpected(*err);
    }
    const sim::CombatRules rules{.monster_think_hz = *think,
                                 .player_respawn = *respawn,
                                 .monster_stall_timeout = *stall_timeout,
                                 .monster_stall_distance_m = *stall_distance,
                                 .range_tolerance = *tolerance,
                                 .monster_home_arrive_m = *arrive,
                                 .monster_pack_ring_radius_m = *ring};
    if (auto valid = rules.Validate(); !valid) {
        return std::unexpected(LoadError{"invalid combat rules: " + valid.error()});
    }
    return rules;
}

std::expected<sim::EconomyRules, LoadError> EconomyFromTable(const toml::table& table) {
    const auto capacity = RequireU32(table, "inventory.capacity");
    const auto range = RequireFloat(table, "pickup.range_m");
    const auto trade_range = RequireFloat(table, "trade.range_m");
    const auto despawn = RequireMs(table, "pickup.despawn_ms");
    const auto scatter = RequireFloat(table, "drops.scatter_radius_m");
    const auto currency = RequireString(table, "currency.primary");
    if (auto err = FirstError(capacity, range, trade_range, despawn, scatter, currency)) {
        return std::unexpected(*err);
    }
    sim::EconomyRules rules{.inventory_capacity = *capacity,
                            .pickup_range_m = *range,
                            .trade_range_m = *trade_range,
                            .ground_item_despawn = *despawn,
                            .drop_scatter_radius_m = *scatter,
                            .primary_currency = *currency};
    if (auto valid = rules.Validate(); !valid) {
        return std::unexpected(LoadError{"invalid economy rules: " + valid.error()});
    }
    return rules;
}

// ---- content ------------------------------------------------------------------------------------

std::expected<sim::SkillKind, LoadError> ParseSkillKind(std::string_view text) {
    if (text == "targeted") {
        return sim::SkillKind::kTargeted;
    }
    if (text == "projectile") {
        return sim::SkillKind::kProjectile;
    }
    return std::unexpected(LoadError{std::format("unknown skill kind '{}'", text)});
}

std::expected<void, LoadError> ParseSkillTable(const toml::table& t, sim::ContentBundle& bundle) {
    const auto id = RequireU32(t, "id");
    const auto name = RequireString(t, "name");
    const auto kind_text = RequireString(t, "kind");
    const auto cost = RequireI32(t, "cost_mp");
    const auto cooldown = RequireMs(t, "cooldown_ms");
    const auto cast = RequireMs(t, "cast_time_ms");
    const auto range = RequireFloat(t, "range_m");
    const auto damage = RequireI32(t, "damage");
    if (auto err = FirstError(id, name, kind_text, cost, cooldown, cast, range, damage)) {
        return std::unexpected(LoadError{"skills: " + err->message});
    }
    const auto kind = ParseSkillKind(*kind_text);
    if (!kind) {
        return std::unexpected(kind.error());
    }
    sim::SkillDefinition skill{.id = sim::SkillId{*id},
                               .name = *name,
                               .kind = *kind,
                               .cost_mp = *cost,
                               .cooldown = *cooldown,
                               .cast_time = *cast,
                               .range_m = *range,
                               .damage = *damage};
    if (*kind == sim::SkillKind::kProjectile) {
        const auto speed = RequireFloat(t, "projectile_speed_mps");
        const auto radius = RequireFloat(t, "projectile_radius_m");
        if (auto err = FirstError(speed, radius)) {
            return std::unexpected(LoadError{std::format("skill {}: {}", *id, err->message)});
        }
        skill.projectile_speed_mps = *speed;
        skill.projectile_radius_m = *radius;
    }
    if (!bundle.skills.emplace(skill.id, std::move(skill)).second) {
        return std::unexpected(LoadError{std::format("duplicate skill id {}", *id)});
    }
    return {};
}

std::expected<void, LoadError> ParseClassTable(const toml::table& t, sim::ContentBundle& bundle) {
    const auto id = RequireU32(t, "id");
    const auto name = RequireString(t, "name");
    const auto hp = RequireI32(t, "max_hp");
    const auto mp = RequireI32(t, "max_mp");
    const auto regen = RequireFloat(t, "mp_regen_per_sec");
    if (auto err = FirstError(id, name, hp, mp, regen)) {
        return std::unexpected(LoadError{"classes: " + err->message});
    }
    sim::ClassDefinition cls{.id = sim::ClassId{*id},
                             .name = *name,
                             .max_hp = *hp,
                             .max_mp = *mp,
                             .mp_regen_per_sec = *regen};
    const toml::array* skill_ids = t.at_path("skills").as_array();
    if (skill_ids == nullptr) {
        return std::unexpected(LoadError{std::format("class {}: missing skills array", *id)});
    }
    for (const toml::node& n : *skill_ids) {
        const auto v = n.value<std::int64_t>();
        if (!v || *v <= 0) {
            return std::unexpected(
                LoadError{std::format("class {}: skills must be positive ids", *id)});
        }
        cls.skills.emplace_back(static_cast<std::uint32_t>(*v));
    }
    if (!bundle.classes.emplace(cls.id, std::move(cls)).second) {
        return std::unexpected(LoadError{std::format("duplicate class id {}", *id)});
    }
    return {};
}

std::expected<void, LoadError> ParseMonsterTable(const toml::table& t, sim::ContentBundle& bundle) {
    const auto id = RequireU32(t, "template_id");
    const auto name = RequireString(t, "name");
    const auto hp = RequireI32(t, "max_hp");
    const auto speed = RequireFloat(t, "speed_mps");
    const auto aggro = RequireFloat(t, "aggro_radius_m");
    const auto leash = RequireFloat(t, "leash_radius_m");
    const auto range = RequireFloat(t, "attack_range_m");
    const auto damage = RequireI32(t, "attack_damage");
    const auto cooldown = RequireMs(t, "attack_cooldown_ms");
    const auto respawn = RequireMs(t, "respawn_ms");
    if (auto err =
            FirstError(id, name, hp, speed, aggro, leash, range, damage, cooldown, respawn)) {
        return std::unexpected(LoadError{"monsters: " + err->message});
    }
    sim::MonsterDefinition monster{.id = sim::MonsterTemplateId{*id},
                                   .name = *name,
                                   .max_hp = *hp,
                                   .speed_mps = *speed,
                                   .aggro_radius_m = *aggro,
                                   .leash_radius_m = *leash,
                                   .attack_range_m = *range,
                                   .attack_damage = *damage,
                                   .attack_cooldown = *cooldown,
                                   .respawn = *respawn};
    if (!bundle.monsters.emplace(monster.id, std::move(monster)).second) {
        return std::unexpected(LoadError{std::format("duplicate monster template {}", *id)});
    }
    return {};
}

std::expected<void, LoadError> ParseItemTable(const toml::table& t, sim::ContentBundle& bundle) {
    const auto id = RequireU32(t, "id");
    const auto name = RequireString(t, "name");
    if (auto err = FirstError(id, name)) {
        return std::unexpected(LoadError{"items: " + err->message});
    }
    // Optional, but a value that is not a boolean is a mistake, not "not stackable".
    const toml::node_view stackable = t.at_path("stackable");
    if (stackable && !stackable.is_boolean()) {
        return std::unexpected(
            LoadError{std::format("item {}: 'stackable' must be true or false", *id)});
    }
    const sim::ItemDefinition item{
        .id = sim::ItemTemplateId{*id}, .name = *name, .stackable = stackable.value_or(false)};
    if (!bundle.items.emplace(item.id, item).second) {
        return std::unexpected(LoadError{std::format("duplicate item id {}", *id)});
    }
    return {};
}

std::expected<void, LoadError> ParseDropTable(const toml::table& t, sim::ContentBundle& bundle) {
    const auto monster = RequireU32(t, "monster_template_id");
    const auto gold = RequireInt(t, "gold");
    if (auto err = FirstError(monster, gold)) {
        return std::unexpected(LoadError{"drop_tables: " + err->message});
    }
    sim::DropTable table{.monster = sim::MonsterTemplateId{*monster}, .gold = *gold};
    auto entries =
        ForEachTable(t, "entries", [&](const toml::table& e) -> std::expected<void, LoadError> {
            const auto item = RequireU32(e, "item_template_id");
            const auto chance = RequireFloat(e, "chance");
            if (auto err = FirstError(item, chance)) {
                return std::unexpected(
                    LoadError{std::format("drop_tables[{}]: {}", *monster, err->message)});
            }
            table.entries.push_back(
                sim::DropEntry{.item = sim::ItemTemplateId{*item}, .chance = *chance});
            return {};
        });
    if (!entries) {
        return std::unexpected(entries.error());
    }
    if (!bundle.drops.emplace(table.monster, std::move(table)).second) {
        return std::unexpected(
            LoadError{std::format("duplicate drop table for monster {}", *monster)});
    }
    return {};
}

// Every content file, already parsed; the bundle is assembled and cross-validated from these.
struct ContentTables {
    const toml::table& version;
    const toml::table& classes;
    const toml::table& skills;
    const toml::table& monsters;
    const toml::table& items;
    const toml::table& drops;
};

std::expected<sim::ContentBundle, LoadError> ContentFromTables(const ContentTables& tables) {
    sim::ContentBundle bundle;
    const auto ver = RequireString(tables.version, "content.version");
    if (!ver) {
        return std::unexpected(ver.error());
    }
    bundle.version = *ver;
    auto parse = [&bundle](const toml::table& t, auto parser) { return parser(t, bundle); };
    const auto section = [&](const toml::table& table, std::string_view key, auto parser) {
        return ForEachTable(table, key, [&](const toml::table& t) { return parse(t, parser); });
    };
    if (auto r = section(tables.skills, "skills", ParseSkillTable); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = section(tables.classes, "classes", ParseClassTable); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = section(tables.monsters, "monsters", ParseMonsterTable); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = section(tables.items, "items", ParseItemTable); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = section(tables.drops, "drop_tables", ParseDropTable); !r) {
        return std::unexpected(r.error());
    }
    if (auto valid = bundle.Validate(); !valid) {
        return std::unexpected(LoadError{"invalid content: " + valid.error()});
    }
    return bundle;
}

}  // namespace

// ---- public API ---------------------------------------------------------------------------------

std::expected<sim::MapData, LoadError> LoadMap(const std::filesystem::path& file) {
    auto table = ParseFile(file);
    return table ? MapFromTable(*table) : std::unexpected(table.error());
}

std::expected<sim::MapData, LoadError> ParseMap(std::string_view toml_text) {
    auto table = ParseText(toml_text, "map text");
    return table ? MapFromTable(*table) : std::unexpected(table.error());
}

std::expected<sim::MovementRules, LoadError> LoadMovementRules(const std::filesystem::path& file) {
    auto table = ParseFile(file);
    return table ? RulesFromTable(*table) : std::unexpected(table.error());
}

std::expected<sim::MovementRules, LoadError> ParseMovementRules(std::string_view toml_text) {
    auto table = ParseText(toml_text, "movement rules text");
    return table ? RulesFromTable(*table) : std::unexpected(table.error());
}

std::expected<sim::CombatRules, LoadError> LoadCombatRules(const std::filesystem::path& file) {
    auto table = ParseFile(file);
    return table ? CombatFromTable(*table) : std::unexpected(table.error());
}

std::expected<sim::CombatRules, LoadError> ParseCombatRules(std::string_view toml_text) {
    auto table = ParseText(toml_text, "combat rules text");
    return table ? CombatFromTable(*table) : std::unexpected(table.error());
}

std::expected<sim::EconomyRules, LoadError> LoadEconomyRules(const std::filesystem::path& file) {
    auto table = ParseFile(file);
    return table ? EconomyFromTable(*table) : std::unexpected(table.error());
}

std::expected<sim::EconomyRules, LoadError> ParseEconomyRules(std::string_view toml_text) {
    auto table = ParseText(toml_text, "economy rules text");
    return table ? EconomyFromTable(*table) : std::unexpected(table.error());
}

std::expected<sim::ContentBundle, LoadError> LoadContent(const std::filesystem::path& root) {
    auto version = ParseFile(root / "version.toml");
    auto classes = ParseFile(root / "classes" / "classes.toml");
    auto skills = ParseFile(root / "skills" / "skills.toml");
    auto monsters = ParseFile(root / "monsters" / "monsters.toml");
    auto items = ParseFile(root / "items" / "items.toml");
    auto drops = ParseFile(root / "drops" / "drops.toml");
    if (auto err = FirstError(version, classes, skills, monsters, items, drops)) {
        return std::unexpected(*err);
    }
    return ContentFromTables({.version = *version,
                              .classes = *classes,
                              .skills = *skills,
                              .monsters = *monsters,
                              .items = *items,
                              .drops = *drops});
}

std::expected<sim::ContentBundle, LoadError> ParseContent(const ContentTexts& texts) {
    auto version = ParseText(texts.version, "version text");
    auto classes = ParseText(texts.classes, "classes text");
    auto skills = ParseText(texts.skills, "skills text");
    auto monsters = ParseText(texts.monsters, "monsters text");
    auto items = ParseText(texts.items, "items text");
    auto drops = ParseText(texts.drops, "drops text");
    if (auto err = FirstError(version, classes, skills, monsters, items, drops)) {
        return std::unexpected(*err);
    }
    return ContentFromTables({.version = *version,
                              .classes = *classes,
                              .skills = *skills,
                              .monsters = *monsters,
                              .items = *items,
                              .drops = *drops});
}

}  // namespace sa::zone::data
