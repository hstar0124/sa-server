#include "sa/zone/data/game_data_loader.hpp"

#include <toml++/toml.hpp>

#include <format>
#include <optional>
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

std::expected<float, LoadError> RequireFloat(const toml::table& table, std::string_view key) {
    const toml::node_view node = table.at_path(key);
    if (const auto value = node.value<double>()) {
        return static_cast<float>(*value);
    }
    return std::unexpected(LoadError{std::format("missing or non-numeric '{}'", key)});
}

std::expected<std::uint32_t, LoadError> RequireU32(const toml::table& table, std::string_view key) {
    const toml::node_view node = table.at_path(key);
    if (const auto value = node.value<std::int64_t>();
        value && *value >= 0 && std::in_range<std::uint32_t>(*value)) {
        return static_cast<std::uint32_t>(*value);
    }
    return std::unexpected(LoadError{std::format("missing or out-of-range integer '{}'", key)});
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

std::expected<sim::MapData, LoadError> MapFromTable(const toml::table& table) {
    sim::MapData map;
    const auto width = RequireFloat(table, "map.width");
    const auto height = RequireFloat(table, "map.height");
    const auto cell = RequireFloat(table, "map.cell_size");
    for (const auto* err : {width ? nullptr : &width.error(), height ? nullptr : &height.error(),
                            cell ? nullptr : &cell.error()}) {
        if (err != nullptr) {
            return std::unexpected(*err);
        }
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
    if (auto valid = map.Validate(); !valid) {
        return std::unexpected(LoadError{"invalid map: " + valid.error()});
    }
    return map;
}

std::expected<sim::MovementRules, LoadError> RulesFromTable(const toml::table& table) {
    const auto speed = RequireFloat(table, "movement.max_speed_mps");
    const auto enter = RequireFloat(table, "aoi.enter_radius_m");
    const auto leave = RequireFloat(table, "aoi.leave_radius_m");
    const auto budget = RequireU32(table, "input.commands_per_tick_budget");
    for (const auto* err : {speed ? nullptr : &speed.error(), enter ? nullptr : &enter.error(),
                            leave ? nullptr : &leave.error(), budget ? nullptr : &budget.error()}) {
        if (err != nullptr) {
            return std::unexpected(*err);
        }
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

}  // namespace

std::expected<sim::MapData, LoadError> LoadMap(const std::filesystem::path& file) {
    auto table =
        TryParse([&] { return toml::parse_file(file.string()); }, "'" + file.string() + "'");
    if (!table) {
        return std::unexpected(table.error());
    }
    return MapFromTable(*table);
}

std::expected<sim::MapData, LoadError> ParseMap(std::string_view toml_text) {
    auto table = TryParse([&] { return toml::parse(toml_text); }, "map text");
    if (!table) {
        return std::unexpected(table.error());
    }
    return MapFromTable(*table);
}

std::expected<sim::MovementRules, LoadError> LoadMovementRules(const std::filesystem::path& file) {
    auto table =
        TryParse([&] { return toml::parse_file(file.string()); }, "'" + file.string() + "'");
    if (!table) {
        return std::unexpected(table.error());
    }
    return RulesFromTable(*table);
}

std::expected<sim::MovementRules, LoadError> ParseMovementRules(std::string_view toml_text) {
    auto table = TryParse([&] { return toml::parse(toml_text); }, "movement rules text");
    if (!table) {
        return std::unexpected(table.error());
    }
    return RulesFromTable(*table);
}

}  // namespace sa::zone::data
