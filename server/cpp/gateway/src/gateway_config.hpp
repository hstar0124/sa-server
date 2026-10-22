#pragma once

#include "sa/common/config_source.hpp"
#include "sa/common/framing.hpp"
#include "sa/common/ids.hpp"
#include "sa/common/logging.hpp"

#include "entry_ticket_verifier.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace sa::gateway {

struct ZoneEndpoint {
    common::ZoneId zone;
    std::string address;
    std::uint16_t port = 0;
};

struct GatewayConfig {
    // [listen] client-facing
    std::string listen_address;
    std::uint16_t listen_port = 0;
    std::uint32_t max_connections = 0;
    // [websocket] browser viewer transport (ADR-0004); same protocol, different framing
    bool websocket_enabled = false;
    std::string websocket_address;
    std::uint16_t websocket_port = 0;
    // [protocol]
    common::FrameLimits frame_limits;
    std::uint32_t max_messages_per_second = 0;
    std::chrono::milliseconds hello_timeout{};
    std::chrono::milliseconds idle_timeout{};  // after the first message, see SessionProtocolConfig
    // How long closing a client connection may wait for the client to take our last bytes and
    // close its side, before the connection is reset and its slot freed.
    std::chrono::milliseconds client_close_linger{};
    // [ticket]
    std::vector<std::byte> ticket_key;
    TicketKeyId ticket_key_id;
    std::chrono::milliseconds ticket_max_clock_skew{};
    // [backend]
    std::string backend_address;
    std::chrono::milliseconds backend_rpc_timeout{};
    std::chrono::milliseconds lease_renew_interval{};
    std::chrono::milliseconds lease_safety_margin{};
    // [zone_link] + [[zones]]
    common::FrameLimits zone_link_frame_limits;
    std::chrono::milliseconds zone_link_reconnect_backoff{};
    std::chrono::milliseconds zone_link_close_linger{};        // bounds closing a link at shutdown
    std::chrono::milliseconds zone_link_establish_timeout{};   // connect plus LinkWelcome
    std::chrono::milliseconds zone_link_heartbeat_interval{};  // LinkPing cadence while ready
    std::chrono::milliseconds zone_link_silence_timeout{};     // nothing heard: the link is dead
    std::vector<ZoneEndpoint> zones;
    // How long shutdown waits after kicking every session before it closes the zone links, so
    // detaches reach the zones and EndSession calls are answered. Longer than the RPC timeout.
    std::chrono::milliseconds shutdown_grace{};

    common::LoggingConfig logging;

    static constexpr std::string_view kEnvPrefix = "SA_GATEWAY__";

    [[nodiscard]] static std::expected<GatewayConfig, common::ConfigError> FromConfig(
        const common::ConfigSource& source);
};

}  // namespace sa::gateway
