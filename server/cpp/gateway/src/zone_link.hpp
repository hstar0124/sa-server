#pragma once

#include "sa/common/framed_connection.hpp"
#include "sa/common/framing.hpp"
#include "sa/common/ids.hpp"
#include "sa/common/instance_id.hpp"

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>

#include <internal/zone_link.pb.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace sa::gateway {

struct ZoneLinkConfig {
    common::ZoneId zone;
    std::string address;
    std::uint16_t port = 0;
    common::FrameLimits frame_limits;
    std::chrono::milliseconds reconnect_backoff{};
    std::chrono::milliseconds close_linger{};
};

// Persistent connection from this gateway to one zone, carrying all sessions routed there.
// Reconnects on failure; while down, every session on that zone is dropped (the zone despawns
// them when it sees the link close, so both sides agree).
class ZoneLink : public std::enable_shared_from_this<ZoneLink> {
public:
    class Handler {
    public:
        virtual ~Handler() = default;
        virtual void OnZoneReady(common::ZoneId zone, const internal::v1::LinkWelcome& welcome) = 0;
        virtual void OnZoneDown(common::ZoneId zone) = 0;
        virtual void OnSessionAttached(const internal::v1::SessionAttached& attached) = 0;
        virtual void OnSessionRejected(const internal::v1::SessionRejected& rejected) = 0;
        virtual void OnSessionEvicted(const internal::v1::SessionEvicted& evicted) = 0;
        virtual void OnSessionOutput(const internal::v1::SessionOutput& output) = 0;
    };

    ZoneLink(asio::io_context& io, ZoneLinkConfig config, common::InstanceId gateway_instance,
             Handler& handler);

    void Start();
    void Stop();

    [[nodiscard]] bool is_ready() const noexcept { return state_ == State::kReady; }
    [[nodiscard]] common::ZoneId zone() const noexcept { return config_.zone; }

    // Session traffic; silently dropped when the link is not ready (the caller sees OnZoneDown).
    void Attach(const internal::v1::SessionAttach& attach);
    void Detach(const common::InstanceId& session, internal::v1::DetachReason reason);
    void Forward(const common::InstanceId& session, const game::v1::ClientMessage& message);

private:
    enum class State : std::uint8_t { kStopped, kConnecting, kHandshaking, kReady, kBackoff };

    void Connect();
    void OnConnected();
    void OnFrame(std::span<const std::byte> payload);
    void OnClosed(const std::error_code& ec);
    void ScheduleReconnect(const std::string& why);
    void Send(const internal::v1::GatewayToZone& message);

    asio::io_context& io_;
    ZoneLinkConfig config_;
    common::InstanceId gateway_instance_;
    Handler& handler_;
    asio::ip::tcp::resolver resolver_;
    // The socket of a connect in progress, so Stop can cancel it: a connect to a host that does
    // not answer can take minutes, and shutdown waits for outstanding work.
    std::shared_ptr<asio::ip::tcp::socket> connecting_;
    asio::steady_timer backoff_timer_;
    std::shared_ptr<common::FramedConnection> connection_;
    State state_ = State::kStopped;
    std::uint32_t consecutive_failures_ = 0;  // first failure is a warning, the rest are debug
};

}  // namespace sa::gateway
