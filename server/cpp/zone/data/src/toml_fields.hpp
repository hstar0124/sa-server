#pragma once

#include "sa/zone/data/game_data_loader.hpp"

#include <toml++/toml.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Typed access to parsed TOML for the game-data loader: every field is read through these, so a
// missing key, a wrong type or a non-finite number fails the load with the key's name.
namespace sa::zone::data::toml_fields {

// A parse failure becomes a LoadError naming the file or the text.
[[nodiscard]] std::expected<toml::table, LoadError> ParseFile(const std::filesystem::path& file);

[[nodiscard]] std::expected<toml::table, LoadError> ParseText(std::string_view text,
                                                              const char* what);

// Each Require* names the key in its error: missing, of the wrong type, out of range, or (for
// floats) not finite.
[[nodiscard]] std::expected<float, LoadError> RequireFloat(const toml::table& table,
                                                           std::string_view key);

[[nodiscard]] std::expected<std::int64_t, LoadError> RequireInt(const toml::table& table,
                                                                std::string_view key);

[[nodiscard]] std::expected<std::uint32_t, LoadError> RequireU32(const toml::table& table,
                                                                 std::string_view key);

[[nodiscard]] std::expected<std::int32_t, LoadError> RequireI32(const toml::table& table,
                                                                std::string_view key);

[[nodiscard]] std::expected<std::chrono::milliseconds, LoadError> RequireMs(
    const toml::table& table, std::string_view key);

[[nodiscard]] std::expected<std::string, LoadError> RequireString(const toml::table& table,
                                                                  std::string_view key);

// Reads an array of fixed-size numeric arrays, e.g. [[x, y], [x, y]].
[[nodiscard]] std::expected<std::vector<std::vector<float>>, LoadError> RequireTuples(
    const toml::table& table, std::string_view key, std::size_t arity);

[[nodiscard]] std::expected<std::vector<float>, LoadError> RequirePair(const toml::table& table,
                                                                       std::string_view key);

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

// The first error among several field reads, so a table's fields can be read in one go and
// checked once.
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

}  // namespace sa::zone::data::toml_fields
