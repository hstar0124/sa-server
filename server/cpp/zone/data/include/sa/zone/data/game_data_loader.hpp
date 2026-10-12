#pragma once

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

}  // namespace sa::zone::data
