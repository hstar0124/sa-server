#include "toml_fields.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace sa::zone::data::toml_fields {
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

// A number as a float, or nothing when it is absent, not a number, NaN, infinite or beyond the
// float range (converting such a double is undefined). NaN would slip past every "<= 0" check.
template <typename Node>
std::optional<float> FiniteFloat(const Node& node) {
    const auto value = node.template value<double>();
    if (!value || !std::isfinite(*value) ||
        std::abs(*value) > static_cast<double>(std::numeric_limits<float>::max())) {
        return std::nullopt;
    }
    return static_cast<float>(*value);
}

}  // namespace

std::expected<toml::table, LoadError> ParseFile(const std::filesystem::path& file) {
    return TryParse([&] { return toml::parse_file(file.string()); }, "'" + file.string() + "'");
}

std::expected<toml::table, LoadError> ParseText(std::string_view text, const char* what) {
    return TryParse([&] { return toml::parse(text); }, what);
}

std::expected<float, LoadError> RequireFloat(const toml::table& table, std::string_view key) {
    if (const auto value = FiniteFloat(table.at_path(key))) {
        return *value;
    }
    return std::unexpected(LoadError{std::format("missing or non-finite number '{}'", key)});
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
            const auto v = FiniteFloat(n);
            if (!v) {
                return std::unexpected(
                    LoadError{std::format("'{}' contains a non-finite number", key)});
            }
            tuple.push_back(*v);
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
        const auto v = FiniteFloat(n);
        if (!v) {
            return std::unexpected(
                LoadError{std::format("'{}' contains a non-finite number", key)});
        }
        out.push_back(*v);
    }
    return out;
}

}  // namespace sa::zone::data::toml_fields
