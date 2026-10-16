#pragma once

#include "sa/common/framed_connection.hpp"
#include "sa/common/instance_id.hpp"

#include "link_sink.hpp"

#include <asio/ip/tcp.hpp>

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

    GatewayLink(asio::ip::tcp::socket socket, common::FrameLimits limits, Handler& handler);

    void Start();
    void Send(const internal::v1::ZoneToGateway& message) override;
    void Close();

    [[nodiscard]] const std::optional<common::InstanceId>& gateway_id() const noexcept {
        return gateway_id_;
    }
    [[nodiscard]] std::string PeerAddress() const { return connection_->PeerAddress(); }

private:
    void OnFrame(std::span<const std::byte> payload);
    void OnClosed(const std::error_code& reason);

    std::shared_ptr<common::FramedConnection> connection_;
    Handler& handler_;
    std::optional<common::InstanceId> gateway_id_;  // set by LinkHello
    bool closed_notified_ = false;
};

}  // namespace sa::zone
