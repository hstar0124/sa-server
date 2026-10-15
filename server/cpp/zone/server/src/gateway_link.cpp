#include "gateway_link.hpp"

#include <spdlog/spdlog.h>

namespace sa::zone {

GatewayLink::GatewayLink(asio::ip::tcp::socket socket, common::FrameLimits limits, Handler& handler)
    : connection_(std::make_shared<common::FramedConnection>(std::move(socket), limits)),
      handler_(handler) {}

void GatewayLink::Start() {
    connection_->Start(common::FramedConnection::Callbacks{
        .on_frame = [self = shared_from_this()](std::span<const std::byte> p) { self->OnFrame(p); },
        .on_closed = [self = shared_from_this()](const std::error_code& ec) { self->OnClosed(ec); },
    });
}

void GatewayLink::Send(const internal::v1::ZoneToGateway& message) {
    if (!connection_->Send(message)) {
        spdlog::warn("gateway link {}: outbound message dropped (too large or link closed)",
                     PeerAddress());
    }
}

void GatewayLink::Close() {
    connection_->Close();
}

void GatewayLink::OnFrame(std::span<const std::byte> payload) {
    internal::v1::GatewayToZone message;
    if (!message.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        spdlog::warn("gateway link {}: unparseable frame, closing", PeerAddress());
        connection_->Abort();
        return;
    }
    using internal::v1::GatewayToZone;
    if (!gateway_id_ && message.body_case() != GatewayToZone::kLinkHello) {
        spdlog::warn("gateway link {}: message before LinkHello, closing", PeerAddress());
        connection_->Abort();
        return;
    }
    switch (message.body_case()) {
        case GatewayToZone::kLinkHello: {
            const auto id =
                common::InstanceId::FromString(message.link_hello().gateway_instance_id());
            if (!id || gateway_id_) {
                spdlog::warn("gateway link {}: invalid or repeated LinkHello, closing",
                             PeerAddress());
                connection_->Abort();
                return;
            }
            gateway_id_ = *id;
            handler_.OnLinkReady(*this);
            return;
        }
        case GatewayToZone::kLinkPing: {
            internal::v1::ZoneToGateway pong;
            pong.mutable_link_pong()->set_nonce(message.link_ping().nonce());
            Send(pong);
            return;
        }
        case GatewayToZone::kSessionAttach:
            handler_.OnAttach(*this, message.session_attach());
            return;
        case GatewayToZone::kSessionDetach:
            handler_.OnDetach(*this, message.session_detach());
            return;
        case GatewayToZone::kSessionInput:
            handler_.OnInput(*this, message.session_input());
            return;
        case GatewayToZone::BODY_NOT_SET:
            break;
    }
    spdlog::warn("gateway link {}: empty message, closing", PeerAddress());
    connection_->Abort();
}

void GatewayLink::OnClosed(const std::error_code& reason) {
    if (closed_notified_) {
        return;
    }
    closed_notified_ = true;
    spdlog::info("gateway link {} closed ({})", PeerAddress(),
                 reason ? reason.message() : "orderly");
    handler_.OnLinkClosed(*this);
}

}  // namespace sa::zone
