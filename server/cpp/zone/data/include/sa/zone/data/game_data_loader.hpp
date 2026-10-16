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

// version.toml, classes/classes.toml, skills/skills.toml and monsters/monsters.toml under `root`,
// validated as a bundle.
[[nodiscard]] std::expected<sim::ContentBundle, LoadError> LoadContent(
    const std::filesystem::path& root);
[[nodiscard]] std::expected<sim::ContentBundle, LoadError> ParseContent(
    std::string_view version_toml, std::string_view classes_toml, std::string_view skills_toml,
    std::string_view monsters_toml);

}  // namespace sa::zone::data
