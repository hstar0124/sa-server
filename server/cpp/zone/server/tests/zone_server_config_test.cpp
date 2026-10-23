#include "zone_server_config.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace sa::zone {
namespace {

std::string ShippedConfig() {
    const std::ifstream file{SA_ZONE_CONFIG};
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

std::expected<ZoneServerConfig, common::ConfigError> Parse(const std::string& text) {
    auto source = common::ConfigSource::FromText(text, ZoneServerConfig::kEnvPrefix);
    if (!source) {
        return std::unexpected(source.error());
    }
    return ZoneServerConfig::FromConfig(*source);
}

std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    if (at == std::string::npos) {
        throw std::logic_error{"the shipped zone config no longer contains '" + from + "'"};
    }
    return text.replace(at, from.size(), to);
}

TEST(ZoneServerConfig, TheShippedConfigIsValid) {
    const auto config = Parse(ShippedConfig());
    ASSERT_TRUE(config.has_value()) << config.error().message;
    EXPECT_FALSE(config->rng_seed.has_value()) << "absent: a seed from the OS";
    EXPECT_EQ(config->TicksPerCheckpoint(), 300U) << "15 s at 20 Hz";
}

TEST(ZoneServerConfig, ZeroLimitsAndIntervalsAreRefused) {
    for (const auto& [from, to] :
         {std::pair{"stats_log_interval_sec = 10", "stats_log_interval_sec = 0"},
          std::pair{"max_frame_bytes = 262144", "max_frame_bytes = 0"},
          std::pair{"max_gateways = 8", "max_gateways = 0"},
          std::pair{"max_segment_bytes = 16384", "max_segment_bytes = 0"},
          std::pair{"call_attempts = 3", "call_attempts = 0"},
          std::pair{"checkpoint_interval_ms = 15000", "checkpoint_interval_ms = 1"}}) {
        EXPECT_FALSE(Parse(Replace(ShippedConfig(), from, to)).has_value()) << to;
    }
}

}  // namespace
}  // namespace sa::zone
