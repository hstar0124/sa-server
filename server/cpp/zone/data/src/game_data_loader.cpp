#include "sa/zone/data/game_data_loader.hpp"

#include <toml++/toml.hpp>

#include <chrono>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace sa::zone::data {
namespace {

template <typename ParseFn>
std::expected<toml::table, LoadError> TryParse(ParseFn&& parse, const std::string& what) {
    try {
        return std::forward<ParseFn>(parse)();
    } catch (const toml::parse_error& error) {
        return std::unexpected(
            LoadError{std::format("cannot parse {}: {}", what, error.description())});
    }
}

std::expected<toml::table, LoadError> ParseFile(const std::filesystem::path& file) {
    return TryParse([&] { return toml::parse_file(file.string()); }, "'" + file.string() + "'");
}

std::expected<toml::table, LoadError> ParseText(std::string_view text, const char* what) {
    return TryParse([&] { return toml::parse(text); }, what);
}

// ---- typed field access -------------------------------------------------------------------------

std::expected<float, LoadError> RequireFloat(const toml::table& table, std::string_view key) {
    if (const auto value = table.at_path(key).value<double>()) {
        return static_cast<float>(*value);
    }
    return std::unexpected(LoadError{std::format("missing or non-numeric '{}'", key)});
}

std::expected<std::int64_t, LoadError> RequireInt(const toml::table& table, std::string_view key) {
    if (const auto value = table.at_path(key).value<std::int64_t>()) {
        return *value;
    }
    return std::unexpected(LoadError{std::format("missing or non-integer '{}'", key)});
}

std::expected<std::uint32_t, LoadError> RequireU32(const toml::table& table, std::string_view key) {
    const auto value = RequireInt(table, key);
    if (!value) {
        return std::unexpected(value.error());
    }
    if (*value < 0 || !std::in_range<std::uint32_t>(*value)) {
        return std::unexpected(LoadError{std::format("'{}' is out of range", key)});
    }
    return static_cast<std::uint32_t>(*value);
}

std::expected<std::int32_t, LoadError> RequireI32(const toml::table& table, std::string_view key) {
    const auto value = RequireInt(table, key);
    if (!value) {
        return std::unexpected(value.error());
    }
    if (!std::in_range<std::int32_t>(*value)) {
        return std::unexpected(LoadError{std::format("'{}' is out of range", key)});
    }
    return static_cast<std::int32_t>(*value);
}

std::expected<std::chrono::milliseconds, LoadError> RequireMs(const toml::table& table,
                                                              std::string_view key) {
    const auto value = RequireInt(table, key);
    if (!value) {
        return std::unexpected(value.error());
    }
    return std::chrono::milliseconds{*value};
}

std::expected<std::string, LoadError> RequireString(const toml::table& table,
                                                    std::string_view key) {
    if (const auto value = table.at_path(key).value<std::string>()) {
        return *value;
    }
    return std::unexpected(LoadError{std::format("missing or non-string '{}'", key)});
}

// Reads an array of fixed-size numeric arrays, e.g. [[x, y], [x, y]].
std::expected<std::vector<std::vector<float>>, LoadError> RequireTuples(const toml::table& table,
                                                                        std::string_view key,
                                                                        std::size_t arity) {
    const toml::array* outer = table.at_path(key).as_array();
    if (outer == nullptr) {
        return std::unexpected(LoadError{std::format("missing array '{}'", key)});
    }
    std::vector<std::vector<float>> out;
    for (const toml::node& item : *outer) {
        const toml::array* inner = item.as_array();
        if (inner == nullptr || inner->size() != arity) {
            return std::unexpected(
                LoadError{std::format("'{}' entries must be arrays of {} numbers", key, arity)});
        }
        std::vector<float> tuple;
        for (const toml::node& n : *inner) {
            const auto v = n.value<double>();
            if (!v) {
                return std::unexpected(LoadError{std::format("'{}' contains a non-number", key)});
            }
            tuple.push_back(static_cast<float>(*v));
        }
        out.push_back(std::move(tuple));
    }
    return out;
}

std::expected<std::vector<float>, LoadError> RequirePair(const toml::table& table,
                                                         std::string_view key) {
    const toml::array* array = table.at_path(key).as_array();
    if (array == nullptr || array->size() != 2) {
        return std::unexpected(LoadError{std::format("'{}' must be [x, y]", key)});
    }
    std::vector<float> out;
    for (const toml::node& n : *array) {
        const auto v = n.value<double>();
        if (!v) {
            return std::unexpected(LoadError{std::format("'{}' contains a non-number", key)});
        }
        out.push_back(static_cast<float>(*v));
    }
    return out;
}

// Iterates [[name]] tables; the callback returns an error to stop.
template <typename Fn>
std::expected<void, LoadError> ForEachTable(const toml::table& table, std::string_view key,
                                            const Fn& fn) {
    const toml::array* array = table.at_path(key).as_array();
    if (array == nullptr) {
        return {};  // absent is "none"
    }
    if (!array->is_array_of_tables()) {
        return std::unexpected(LoadError{std::format("'{}' must be an array of tables", key)});
    }
    for (const toml::node& item : *array) {
        if (auto result = fn(*item.as_table()); !result) {
            return std::unexpected(result.error());
        }
    }
    return {};
}

template <typename... Expected>
std::optional<LoadError> FirstError(const Expected&... results) {
    std::optional<LoadError> first;
    auto check = [&](const auto& r) {
        if (!first && !r) {
            first = r.error();
        }
    };
    (check(results), ...);
    return first;
}

// ---- map
// ------------------------------------------------------------------------------------------

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
    if (auto valid = map.Validate(); !valid) {
        return std::unexpected(LoadError{"invalid map: " + valid.error()});
    }
    return map;
}

// ---- rules
// ----------------------------------------------------------------------------------------

std::expected<sim::MovementRules, LoadError> RulesFromTable(const toml::table& table) {
    const auto speed = RequireFloat(table, "movement.max_speed_mps");
    const auto enter = RequireFloat(table, "aoi.enter_radius_m");
    const auto leave = RequireFloat(table, "aoi.leave_radius_m");
    const auto budget = RequireU32(table, "input.commands_per_tick_budget");
    if (auto err = FirstError(speed, enter, leave, budget)) {
        return std::unexpected(*err);
    }
    const sim::MovementRules rules{.max_speed_mps = *speed,
                                   .aoi_enter_radius_m = *enter,
                                   .aoi_leave_radius_m = *leave,
                                   .commands_per_tick_budget = *budget};
    if (auto valid = rules.Validate(); !valid) {
        return std::unexpected(LoadError{"invalid movement rules: " + valid.error()});
    }
    return rules;
}

std::expected<sim::CombatRules, LoadError> CombatFromTable(const toml::table& table) {
    const auto think = RequireU32(table, "combat.monster_think_hz");
    const auto respawn = RequireMs(table, "combat.player_respawn_ms");
    if (auto err = FirstError(think, respawn)) {
        return std::unexpected(*err);
    }
    const sim::CombatRules rules{.monster_think_hz = *think, .player_respawn = *respawn};
    if (auto valid = rules.Validate(); !valid) {
        return std::unexpected(LoadError{"invalid combat rules: " + valid.error()});
    }
    return rules;
}

// ---- content
// ----------------------------------------------------------------------------------------

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

std::expected<sim::ContentBundle, LoadError> ContentFromTables(const toml::table& version,
                                                               const toml::table& classes,
                                                               const toml::table& skills,
                                                               const toml::table& monsters) {
    sim::ContentBundle bundle;
    const auto ver = RequireString(version, "content.version");
    if (!ver) {
        return std::unexpected(ver.error());
    }
    bundle.version = *ver;
    auto parse = [&bundle](const toml::table& t, auto parser) { return parser(t, bundle); };
    if (auto r = ForEachTable(skills, "skills",
                              [&](const toml::table& t) { return parse(t, ParseSkillTable); });
        !r) {
        return std::unexpected(r.error());
    }
    if (auto r = ForEachTable(classes, "classes",
                              [&](const toml::table& t) { return parse(t, ParseClassTable); });
        !r) {
        return std::unexpected(r.error());
    }
    if (auto r = ForEachTable(monsters, "monsters",
                              [&](const toml::table& t) { return parse(t, ParseMonsterTable); });
        !r) {
        return std::unexpected(r.error());
    }
    if (auto valid = bundle.Validate(); !valid) {
        return std::unexpected(LoadError{"invalid content: " + valid.error()});
    }
    return bundle;
}

}  // namespace

// ---- public API
// --------------------------------------------------------------------------------------

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

std::expected<sim::ContentBundle, LoadError> LoadContent(const std::filesystem::path& root) {
    auto version = ParseFile(root / "version.toml");
    auto classes = ParseFile(root / "classes" / "classes.toml");
    auto skills = ParseFile(root / "skills" / "skills.toml");
    auto monsters = ParseFile(root / "monsters" / "monsters.toml");
    if (auto err = FirstError(version, classes, skills, monsters)) {
        return std::unexpected(*err);
    }
    return ContentFromTables(*version, *classes, *skills, *monsters);
}

std::expected<sim::ContentBundle, LoadError> ParseContent(std::string_view version_toml,
                                                          std::string_view classes_toml,
                                                          std::string_view skills_toml,
                                                          std::string_view monsters_toml) {
    auto version = ParseText(version_toml, "version text");
    auto classes = ParseText(classes_toml, "classes text");
    auto skills = ParseText(skills_toml, "skills text");
    auto monsters = ParseText(monsters_toml, "monsters text");
    if (auto err = FirstError(version, classes, skills, monsters)) {
        return std::unexpected(*err);
    }
    return ContentFromTables(*version, *classes, *skills, *monsters);
}

}  // namespace sa::zone::data
