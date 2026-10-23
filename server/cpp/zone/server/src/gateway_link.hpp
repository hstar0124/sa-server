#pragma once

#include "sa/common/framed_connection.hpp"
#include "sa/common/instance_id.hpp"

#include "link_sink.hpp"

#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>

#include <chrono>
#include <memory>
#include <optional>

namespace sa::zone {

// One gateway's multiplexed connection into this zone. Decodes GatewayToZone frames and hands
// them to the handler; the handler is the ZoneHost, which owns all session state.
class GatewayLink final : public LinkSink, public std::enable_shared_from_this<GatewayLink> {
public:
    class Handler {
    public:
        virtual ~Handler() = default;
        virtual void OnLinkReady(GatewayLink& link) = 0;
        virtual void OnAttach(GatewayLink& link, const internal::v1::SessionAttach& attach) = 0;
        virtual void OnDetach(GatewayLink& link, const internal::v1::SessionDetach& detach) = 0;
        virtual void OnInput(GatewayLink& link, const internal::v1::SessionInput& input) = 0;
        virtual void OnLinkClosed(GatewayLink& link) = 0;
    };

    // `close_linger` bounds how long closing waits for the gateway to close its side, so a
    // stalled gateway cannot hold up the zone's shutdown. A link that carries nothing for
    // `silence_timeout` is closed: the gateway pings well within it, so silence means the
    // gateway vanished without closing, and its players would otherwise stay in the world.
    GatewayLink(asio::ip::tcp::socket socket, common::FrameLimits limits,
                std::chrono::milliseconds close_linger, std::chrono::milliseconds silence_timeout,
                Handler& handler);

    void Start();
    void Send(const internal::v1::ZoneToGateway& message) override;
    [[nodiscard]] std::size_t QueuedBytes() const override { return connection_->QueuedBytes(); }
    void Close();

    [[nodiscard]] const std::optional<common::InstanceId>& gateway_id() const noexcept {
        return gateway_id_;
    }
    [[nodiscard]] std::string PeerAddress() const { return connection_->PeerAddress(); }
    // The longest wait between two frames from the gateway since the last call: how close the
    // link came to its silence timeout.
    [[nodiscard]] std::chrono::milliseconds TakeLongestGap();

private:
    void OnFrame(std::span<const std::byte> payload);
    void OnClosed(const std::error_code& reason);
    void ScheduleSilenceCheck();

    // Declared before the connection: it takes the socket's executor before the socket moves.
    asio::steady_timer silence_timer_;
    std::chrono::milliseconds silence_timeout_;
    std::chrono::steady_clock::time_point last_heard_;
    std::chrono::steady_clock::duration longest_gap_{};
    std::shared_ptr<common::FramedConnection> connection_;
    Handler& handler_;
    std::optional<common::InstanceId> gateway_id_;  // set by LinkHello
    bool closed_notified_ = false;
};

}  // namespace sa::zone
