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

TEST(GatewayConfig, AZoneLinkFrameTooSmallForAClientFrameIsRefused) {
    std::string text = ShippedConfigWithKey();
    const std::string frame = "max_frame_bytes = 262144";
    text.replace(text.find(frame), frame.size(), "max_frame_bytes = 1024");
    const auto config = Parse(text);
    ASSERT_FALSE(config.has_value());
    EXPECT_NE(config.error().message.find("zone_link.max_frame_bytes"), std::string::npos);
}

TEST(GatewayConfig, ASilenceTimeoutNoLongerThanTheHeartbeatIsRefused) {
    std::string text = ShippedConfigWithKey();
    const std::string silence = "silence_timeout_ms = 5000";
    text.replace(text.find(silence), silence.size(), "silence_timeout_ms = 1000");
    const auto config = Parse(text);
    ASSERT_FALSE(config.has_value()) << "every healthy link would look silent";
    EXPECT_NE(config.error().message.find("zone_link.silence_timeout_ms"), std::string::npos);
}

TEST(GatewayConfig, AZeroSendQueueLimitIsRefused) {
    std::string text = ShippedConfigWithKey();
    const std::string limit = "max_send_queue_bytes = 1048576";
    text.replace(text.find(limit), limit.size(), "max_send_queue_bytes = 0");
    EXPECT_FALSE(Parse(text).has_value()) << "every client would be kicked at its first snapshot";
}

}  // namespace
}  // namespace sa::gateway
