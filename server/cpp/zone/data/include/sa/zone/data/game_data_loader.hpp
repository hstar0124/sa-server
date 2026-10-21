#pragma once

#include "sa/zone/sim/content.hpp"
#include "sa/zone/sim/map_data.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace sa::zone::data {

struct LoadError {
    std::string message;
};

[[nodiscard]] std::expected<sim::MapData, LoadError> LoadMap(const std::filesystem::path& file);
[[nodiscard]] std::expected<sim::MapData, LoadError> ParseMap(std::string_view toml_text);

[[nodiscard]] std::expected<sim::MovementRules, LoadError> LoadMovementRules(
    const std::filesystem::path& file);
[[nodiscard]] std::expected<sim::MovementRules, LoadError> ParseMovementRules(
    std::string_view toml_text);

// The [combat] table lives in the same rules file as movement.
[[nodiscard]] std::expected<sim::CombatRules, LoadError> LoadCombatRules(
    const std::filesystem::path& file);
[[nodiscard]] std::expected<sim::CombatRules, LoadError> ParseCombatRules(
    std::string_view toml_text);

// rules/economy.toml: [inventory], [pickup] and [currency] tables.
[[nodiscard]] std::expected<sim::EconomyRules, LoadError> LoadEconomyRules(
    const std::filesystem::path& file);
[[nodiscard]] std::expected<sim::EconomyRules, LoadError> ParseEconomyRules(
    std::string_view toml_text);

// The TOML text of every content file. Sections a test does not care about may stay empty: an
// empty document is a valid file with no entries.
struct ContentTexts {
    std::string_view version;
    std::string_view classes;
    std::string_view skills;
    std::string_view monsters;
    std::string_view items;
    std::string_view drops;
};

// version.toml, classes/classes.toml, skills/skills.toml, monsters/monsters.toml,
// items/items.toml and drops/drops.toml under `root`, validated as a bundle.
[[nodiscard]] std::expected<sim::ContentBundle, LoadError> LoadContent(
    const std::filesystem::path& root);
[[nodiscard]] std::expected<sim::ContentBundle, LoadError> ParseContent(const ContentTexts& texts);

}  // namespace sa::zone::data
