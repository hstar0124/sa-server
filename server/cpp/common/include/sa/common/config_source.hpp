#pragma once

#include <toml++/toml.hpp>

#include <charconv>
#include <concepts>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Process configuration: one TOML file plus environment overrides.
//   file:  [zone] simulation_hz = 20
//   env:   SA_ZONE__ZONE__SIMULATION_HZ=30     (prefix + section + "__" + key, case-insensitive)
// Every tunable is read through Require()/GetOr(); code never embeds the literal.
namespace sa::common {

struct ConfigError {
    std::string message;
};

// First failure among several Require() results, so parsers can read all keys up front and
// report one error instead of nesting a check per key.
template <typename... Expected>
[[nodiscard]] std::optional<ConfigError> FirstConfigError(const Expected&... results) {
    std::optional<ConfigError> first;
    auto check = [&](const auto& r) {
        if (!first && !r) {
            first = r.error();
        }
    };
    (check(results), ...);
    return first;
}

class ConfigSource {
public:
    [[nodiscard]] static std::expected<ConfigSource, ConfigError> Load(
        const std::filesystem::path& file, std::string_view env_prefix);

    // Parse TOML text directly; used by tests and by callers that embed defaults.
    [[nodiscard]] static std::expected<ConfigSource, ConfigError> FromText(
        std::string_view toml_text, std::string_view env_prefix);

    template <typename T>
    [[nodiscard]] std::expected<T, ConfigError> Require(std::string_view dotted_key) const;

    template <typename T>
    [[nodiscard]] T GetOr(std::string_view dotted_key, const T& fallback) const {
        auto value = Require<T>(dotted_key);
        return value ? *value : fallback;
    }

    // Each table of an array-of-tables ([[zones]]) as its own source. Environment overrides do
    // not reach into arrays: repeated sections are deployment topology, not tunables.
    [[nodiscard]] std::expected<std::vector<ConfigSource>, ConfigError> Sections(
        std::string_view array_key) const;

private:
    ConfigSource(toml::table table, std::map<std::string, std::string> env_overrides)
        : table_(std::move(table)), env_overrides_(std::move(env_overrides)) {}

    static std::map<std::string, std::string> CollectEnvOverrides(std::string_view prefix);

    template <typename T>
    [[nodiscard]] std::expected<T, ConfigError> ParseOverride(std::string_view key,
                                                              const std::string& raw) const;

    toml::table table_;
    std::map<std::string, std::string> env_overrides_;  // dotted lower-case key -> raw text
};

// ---- template definitions ----

template <typename T>
std::expected<T, ConfigError> ConfigSource::Require(std::string_view dotted_key) const {
    const std::string key{dotted_key};
    if (auto it = env_overrides_.find(key); it != env_overrides_.end()) {
        return ParseOverride<T>(key, it->second);
    }
    const toml::node_view node = table_.at_path(key);
    if (!node) {
        return std::unexpected(ConfigError{"missing config key '" + key + "'"});
    }
    if constexpr (std::same_as<T, std::string>) {
        if (auto v = node.value<std::string>()) {
            return *v;
        }
    } else if constexpr (std::same_as<T, bool>) {
        if (auto v = node.value<bool>()) {
            return *v;
        }
    } else if constexpr (std::floating_point<T>) {
        if (auto v = node.value<double>()) {
            return static_cast<T>(*v);
        }
    } else if constexpr (std::integral<T>) {
        if (auto v = node.value<std::int64_t>()) {
            if (std::in_range<T>(*v)) {
                return static_cast<T>(*v);
            }
            return std::unexpected(ConfigError{"config key '" + key + "' is out of range"});
        }
    } else {
        static_assert(sizeof(T) == 0, "unsupported config value type");
    }
    return std::unexpected(ConfigError{"config key '" + key + "' has the wrong type"});
}

template <typename T>
std::expected<T, ConfigError> ConfigSource::ParseOverride(std::string_view key,
                                                          const std::string& raw) const {
    const auto fail = [&] {
        return std::unexpected(ConfigError{"environment override for '" + std::string{key} +
                                           "' is not a valid value: '" + raw + "'"});
    };
    if constexpr (std::same_as<T, std::string>) {
        return raw;
    } else if constexpr (std::same_as<T, bool>) {
        if (raw == "true" || raw == "1") {
            return true;
        }
        if (raw == "false" || raw == "0") {
            return false;
        }
        return fail();
    } else {
        T value{};
        // std::from_chars takes a [first, last) pointer range.
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const std::string_view text{raw};
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (ec != std::errc{} || ptr != text.data() + text.size()) {
            return fail();
        }
        // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        return value;
    }
}

}  // namespace sa::common
