#include "gateway_config.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace sa::gateway {
namespace {

// 32 bytes, base64. The shipped file leaves the key empty: it is injected at deploy time.
constexpr std::string_view kTestKeyLine =
    R"(signing_key_base64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")";
constexpr std::string_view kEmptyKeyLine = R"(signing_key_base64 = "")";

std::string ShippedConfigWithKey() {
    const std::ifstream file{SA_GATEWAY_CONFIG};
    std::stringstream text;
    text << file.rdbuf();
    std::string config = text.str();
    const auto at = config.find(kEmptyKeyLine);
    if (at == std::string::npos) {
        throw std::logic_error{"the shipped gateway config no longer has an empty key line"};
    }
    return config.replace(at, kEmptyKeyLine.size(), kTestKeyLine);
}

std::expected<GatewayConfig, common::ConfigError> Parse(const std::string& text) {
    auto source = common::ConfigSource::FromText(text, GatewayConfig::kEnvPrefix);
    if (!source) {
        return std::unexpected(source.error());
    }
    return GatewayConfig::FromConfig(*source);
}

TEST(GatewayConfig, TheShippedConfigIsValid) {
    const auto config = Parse(ShippedConfigWithKey());
    ASSERT_TRUE(config.has_value()) << config.error().message;
    EXPECT_GT(config->shutdown_grace, config->backend_rpc_timeout);
}

TEST(GatewayConfig, AShutdownGraceNoLongerThanTheRpcTimeoutIsRefused) {
    std::string text = ShippedConfigWithKey();
    const std::string grace = "grace_ms = 3000";
    text.replace(text.find(grace), grace.size(), "grace_ms = 2000");  // equal to rpc_timeout_ms
    const auto config = Parse(text);
    ASSERT_FALSE(config.has_value());
    EXPECT_NE(config.error().message.find("shutdown.grace_ms"), std::string::npos);
}

TEST(GatewayConfig, AValueOfTheWrongTypeIsAnErrorNotTheDefault) {
    std::string text = ShippedConfigWithKey();
    const std::string enabled = "enabled = true";
    text.replace(text.find(enabled), enabled.size(), "enabled = \"yes\"");
    const auto config = Parse(text);
    ASSERT_FALSE(config.has_value()) << "a mistyped websocket.enabled used to mean false";
    EXPECT_NE(config.error().message.find("websocket.enabled"), std::string::npos);
}

}  // namespace
}  // namespace sa::gateway
