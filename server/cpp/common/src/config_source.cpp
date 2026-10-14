#include "sa/common/config_source.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>

// POSIX process environment; not declared in <cstdlib>. Read-only use.
extern char**
    environ;  // NOLINT(readability-redundant-declaration,cppcoreguidelines-avoid-non-const-global-variables)

namespace sa::common {
namespace {

std::string ToLower(std::string_view text) {
    std::string out{text};
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// "ZONE__SIMULATION_HZ" -> "zone.simulation_hz"
std::string EnvSuffixToDottedKey(std::string_view suffix) {
    std::string key;
    key.reserve(suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (suffix[i] == '_' && i + 1 < suffix.size() && suffix[i + 1] == '_') {
            key.push_back('.');
            ++i;
        } else {
            key.push_back(suffix[i]);
        }
    }
    return ToLower(key);
}

// toml++ is built with exceptions on, so parsing is wrapped here once and surfaces as a value.
template <typename ParseFn>
std::expected<toml::table, ConfigError> TryParse(ParseFn&& parse, const std::string& what) {
    try {
        return std::forward<ParseFn>(parse)();
    } catch (const toml::parse_error& error) {
        return std::unexpected(
            ConfigError{"cannot parse " + what + ": " + std::string{error.description()}});
    }
}

}  // namespace

std::expected<ConfigSource, ConfigError> ConfigSource::Load(const std::filesystem::path& file,
                                                            std::string_view env_prefix) {
    auto table =
        TryParse([&] { return toml::parse_file(file.string()); }, "'" + file.string() + "'");
    if (!table) {
        return std::unexpected(table.error());
    }
    return ConfigSource{std::move(*table), CollectEnvOverrides(env_prefix)};
}

std::expected<ConfigSource, ConfigError> ConfigSource::FromText(std::string_view toml_text,
                                                                std::string_view env_prefix) {
    auto table = TryParse([&] { return toml::parse(toml_text); }, "config text");
    if (!table) {
        return std::unexpected(table.error());
    }
    return ConfigSource{std::move(*table), CollectEnvOverrides(env_prefix)};
}

std::expected<std::vector<ConfigSource>, ConfigError> ConfigSource::Sections(
    std::string_view array_key) const {
    const toml::node_view node = table_.at_path(array_key);
    if (!node) {
        return std::vector<ConfigSource>{};
    }
    const toml::array* array = node.as_array();
    if (array == nullptr || !array->is_array_of_tables()) {
        return std::unexpected(
            ConfigError{"config key '" + std::string{array_key} + "' must be an array of tables"});
    }
    std::vector<ConfigSource> sections;
    sections.reserve(array->size());
    for (const toml::node& item : *array) {
        sections.emplace_back(ConfigSource{*item.as_table(), {}});
    }
    return sections;
}

std::map<std::string, std::string> ConfigSource::CollectEnvOverrides(std::string_view prefix) {
    std::map<std::string, std::string> overrides;
    if (prefix.empty()) {
        return overrides;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): environ is a C array
    for (char* const* entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        const std::string_view line{*entry};
        if (!line.starts_with(prefix)) {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view name = line.substr(prefix.size(), eq - prefix.size());
        overrides.emplace(EnvSuffixToDottedKey(name), std::string{line.substr(eq + 1)});
    }
    return overrides;
}

}  // namespace sa::common
