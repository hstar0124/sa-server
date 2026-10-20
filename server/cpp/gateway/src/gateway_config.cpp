#include "gateway_config.hpp"

#include "sa/common/bytes.hpp"

#include <initializer_list>

namespace sa::gateway {
namespace {}  // namespace

std::expected<GatewayConfig, common::ConfigError> GatewayConfig::FromConfig(
    const common::ConfigSource& source) {
    const auto address = source.Require<std::string>("listen.address");
    const auto port = source.Require<std::uint16_t>("listen.port");
    const auto max_connections = source.Require<std::uint32_t>("listen.max_connections");
    const auto max_msgs = source.Require<std::uint32_t>("protocol.max_messages_per_second");
    const auto client_linger = source.Require<std::uint32_t>("protocol.close_linger_ms");
    const auto hello_timeout = source.Require<std::uint32_t>("protocol.hello_timeout_ms");
    const auto key_b64 = source.Require<std::string>("ticket.signing_key_base64");
    const auto key_id = source.Require<std::uint32_t>("ticket.key_id");
    const auto skew = source.Require<std::uint64_t>("ticket.max_clock_skew_ms");
    const auto backend_address = source.Require<std::string>("backend.address");
    const auto rpc_timeout = source.Require<std::uint32_t>("backend.rpc_timeout_ms");
    const auto renew = source.Require<std::uint32_t>("backend.lease_renew_interval_ms");
    const auto margin = source.Require<std::uint32_t>("backend.lease_safety_margin_ms");
    const auto link_frame = source.Require<std::size_t>("zone_link.max_frame_bytes");
    const auto backoff = source.Require<std::uint32_t>("zone_link.reconnect_backoff_ms");
    const auto link_linger = source.Require<std::uint32_t>("zone_link.close_linger_ms");
    const auto shutdown_grace = source.Require<std::uint32_t>("shutdown.grace_ms");
    if (auto err = common::FirstConfigError(address, port, max_connections, max_msgs, client_linger,
                                            hello_timeout, key_b64, key_id, skew, backend_address,
                                            rpc_timeout, renew, margin, link_frame, backoff,
                                            link_linger, shutdown_grace)) {
        return std::unexpected(*err);
    }
    auto logging = common::LoggingConfig::FromConfig(source);
    if (!logging) {
        return std::unexpected(logging.error());
    }
    auto key = common::DecodeBase64(*key_b64);
    if (!key) {
        return std::unexpected(
            common::ConfigError{"ticket.signing_key_base64 is not valid base64"});
    }
    constexpr std::size_t kMinKeyBytes = 32;
    if (key->size() < kMinKeyBytes) {
        return std::unexpected(
            common::ConfigError{"ticket.signing_key_base64 must decode to at least 32 bytes"});
    }
    if (*max_msgs == 0 || *rpc_timeout == 0 || *renew == 0 || *client_linger == 0 ||
        *link_linger == 0 || *hello_timeout == 0) {
        return std::unexpected(common::ConfigError{"rates and intervals must be positive"});
    }
    if (*shutdown_grace <= *rpc_timeout) {
        return std::unexpected(common::ConfigError{
            "shutdown.grace_ms must exceed backend.rpc_timeout_ms, so the EndSession calls made "
            "when shutdown kicks every session are answered before the zone links close"});
    }

    GatewayConfig out;
    out.listen_address = *address;
    out.listen_port = *port;
    out.max_connections = *max_connections;
    const auto websocket_enabled = source.GetOr<bool>("websocket.enabled", false);
    // The protocol maximum is the contract's; a deployment may only tighten it.
    const auto max_payload =
        source.GetOr<std::size_t>("protocol.max_payload_bytes", common::protocol::kMaxPayloadBytes);
    if (auto err = common::FirstConfigError(websocket_enabled, max_payload)) {
        return std::unexpected(*err);
    }
    out.websocket_enabled = *websocket_enabled;
    if (out.websocket_enabled) {
        const auto ws_address = source.Require<std::string>("websocket.address");
        const auto ws_port = source.Require<std::uint16_t>("websocket.port");
        if (auto err = common::FirstConfigError(ws_address, ws_port)) {
            return std::unexpected(*err);
        }
        out.websocket_address = *ws_address;
        out.websocket_port = *ws_port;
    }
    if (*max_payload == 0 || *max_payload > common::protocol::kMaxPayloadBytes) {
        return std::unexpected(common::ConfigError{
            "protocol.max_payload_bytes must be positive and within the contract maximum"});
    }
    out.frame_limits.max_payload_bytes = *max_payload;
    out.max_messages_per_second = *max_msgs;
    out.client_close_linger = std::chrono::milliseconds{*client_linger};
    out.hello_timeout = std::chrono::milliseconds{*hello_timeout};
    out.ticket_key = std::move(*key);
    out.ticket_key_id = *key_id;
    out.ticket_max_clock_skew_ms = *skew;
    out.backend_address = *backend_address;
    out.backend_rpc_timeout = std::chrono::milliseconds{*rpc_timeout};
    out.lease_renew_interval = std::chrono::milliseconds{*renew};
    out.lease_safety_margin = std::chrono::milliseconds{*margin};
    out.zone_link_frame_limits.max_payload_bytes = *link_frame;
    out.zone_link_reconnect_backoff = std::chrono::milliseconds{*backoff};
    out.zone_link_close_linger = std::chrono::milliseconds{*link_linger};
    out.shutdown_grace = std::chrono::milliseconds{*shutdown_grace};
    out.logging = *logging;

    auto zones = source.Sections("zones");
    if (!zones) {
        return std::unexpected(zones.error());
    }
    if (zones->empty()) {
        return std::unexpected(common::ConfigError{"at least one [[zones]] entry is required"});
    }
    for (const common::ConfigSource& z : *zones) {
        const auto zone_id = z.Require<std::uint32_t>("zone_id");
        const auto zone_address = z.Require<std::string>("address");
        const auto zone_port = z.Require<std::uint16_t>("port");
        if (auto err = common::FirstConfigError(zone_id, zone_address, zone_port)) {
            return std::unexpected(common::ConfigError{"[[zones]]: " + err->message});
        }
        out.zones.push_back(ZoneEndpoint{
            .zone = common::ZoneId{*zone_id}, .address = *zone_address, .port = *zone_port});
    }
    return out;
}

}  // namespace sa::gateway
