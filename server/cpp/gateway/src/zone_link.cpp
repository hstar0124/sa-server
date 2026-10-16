#include "zone_link.hpp"

#include <asio/connect.hpp>
#include <spdlog/spdlog.h>

#include <string>

namespace sa::gateway {

using internal::v1::GatewayToZone;
using internal::v1::ZoneToGateway;

ZoneLink::ZoneLink(asio::io_context& io, ZoneLinkConfig config, common::InstanceId gateway_instance,
                   Handler& handler)
    : io_(io),
      config_(std::move(config)),
      gateway_instance_(gateway_instance),
      handler_(handler),
      resolver_(io),
      backoff_timer_(io) {}

void ZoneLink::Start() {
    if (state_ == State::kStopped) {
        Connect();
    }
}

void ZoneLink::Stop() {
    state_ = State::kStopped;
    backoff_timer_.cancel();
    if (connection_) {
        connection_->Close();
    }
}

// ---- connection lifecycle -----------------------------------------------------------------------

void ZoneLink::Connect() {
    state_ = State::kConnecting;
    resolver_.async_resolve(
        config_.address, std::to_string(config_.port),
        [self = shared_from_this()](const std::error_code& ec,
                                    const asio::ip::tcp::resolver::results_type& results) {
            if (self->state_ != State::kConnecting) {
                return;
            }
            if (ec) {
                self->ScheduleReconnect("resolve " + self->config_.address + ": " + ec.message());
                return;
            }
            auto socket = std::make_shared<asio::ip::tcp::socket>(self->io_);
            asio::async_connect(
                *socket, results,
                [self, socket](const std::error_code& connect_ec, const asio::ip::tcp::endpoint&) {
                    if (self->state_ != State::kConnecting) {
                        return;
                    }
                    if (connect_ec) {
                        self->ScheduleReconnect("connect: " + connect_ec.message());
                        return;
                    }
                    self->connection_ = std::make_shared<common::FramedConnection>(
                        std::move(*socket), self->config_.frame_limits);
                    self->OnConnected();
                });
        });
}

void ZoneLink::OnConnected() {
    state_ = State::kHandshaking;
    connection_->Start(common::MessageConnection::Callbacks{
        .on_message = [self =
                           shared_from_this()](std::span<const std::byte> p) { self->OnFrame(p); },
        .on_closed = [self = shared_from_this()](const std::error_code& ec) { self->OnClosed(ec); },
    });
    GatewayToZone hello;
    hello.mutable_link_hello()->set_gateway_instance_id(gateway_instance_.ToWire());
    Send(hello);
}

void ZoneLink::OnClosed(const std::error_code& ec) {
    const bool was_ready = state_ == State::kReady;
    connection_.reset();
    if (state_ == State::kStopped) {
        return;
    }
    const std::string why = ec ? ec.message() : "closed by zone";
    if (was_ready) {
        spdlog::warn("zone {} link down: {}", config_.zone.value(), why);
        handler_.OnZoneDown(config_.zone);
    }
    ScheduleReconnect(why);
}

void ZoneLink::ScheduleReconnect(const std::string& why) {
    // The first failure is operator-visible; a zone that stays down is reported once, not per try.
    if (++consecutive_failures_ == 1) {
        spdlog::warn("zone {} unreachable ({}); retrying every {} ms", config_.zone.value(), why,
                     config_.reconnect_backoff.count());
    } else {
        spdlog::debug("zone {} still unreachable ({}), attempt {}", config_.zone.value(), why,
                      consecutive_failures_);
    }
    state_ = State::kBackoff;
    backoff_timer_.expires_after(config_.reconnect_backoff);
    backoff_timer_.async_wait([self = shared_from_this()](const std::error_code& ec) {
        if (ec || self->state_ != State::kBackoff) {
            return;
        }
        self->Connect();
    });
}

// ---- inbound
// --------------------------------------------------------------------------------------

void ZoneLink::OnFrame(std::span<const std::byte> payload) {
    ZoneToGateway message;
    if (!message.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        spdlog::warn("zone {}: unparseable frame, reconnecting", config_.zone.value());
        connection_->Abort();
        return;
    }
    if (state_ == State::kHandshaking) {
        if (message.body_case() != ZoneToGateway::kLinkWelcome) {
            spdlog::warn("zone {}: expected LinkWelcome, reconnecting", config_.zone.value());
            connection_->Abort();
            return;
        }
        if (message.link_welcome().zone_id() != config_.zone.value()) {
            spdlog::error("zone link {} answered as zone {}: misconfiguration, reconnecting",
                          config_.zone.value(), message.link_welcome().zone_id());
            connection_->Abort();
            return;
        }
        state_ = State::kReady;
        consecutive_failures_ = 0;
        spdlog::info("zone {} link ready (instance {})", config_.zone.value(),
                     common::InstanceId::FromString(message.link_welcome().zone_instance_id())
                         .value_or(common::InstanceId{})
                         .ToHex());
        handler_.OnZoneReady(config_.zone, message.link_welcome());
        return;
    }
    switch (message.body_case()) {
        case ZoneToGateway::kSessionAttached:
            handler_.OnSessionAttached(message.session_attached());
            return;
        case ZoneToGateway::kSessionRejected:
            handler_.OnSessionRejected(message.session_rejected());
            return;
        case ZoneToGateway::kSessionEvicted:
            handler_.OnSessionEvicted(message.session_evicted());
            return;
        case ZoneToGateway::kSessionOutput:
            handler_.OnSessionOutput(message.session_output());
            return;
        case ZoneToGateway::kLinkPong:
            return;
        case ZoneToGateway::kLinkWelcome:
        case ZoneToGateway::BODY_NOT_SET:
            break;
    }
    spdlog::warn("zone {}: unexpected message {}, reconnecting", config_.zone.value(),
                 static_cast<int>(message.body_case()));
    connection_->Abort();
}

// ---- outbound
// -------------------------------------------------------------------------------------

void ZoneLink::Send(const GatewayToZone& message) {
    if (!connection_ || !connection_->Send(message)) {
        spdlog::warn("zone {}: message dropped (link not open or frame too large)",
                     config_.zone.value());
    }
}

void ZoneLink::Attach(const internal::v1::SessionAttach& attach) {
    if (!is_ready()) {
        return;
    }
    GatewayToZone m;
    *m.mutable_session_attach() = attach;
    Send(m);
}

void ZoneLink::Detach(const common::InstanceId& session, internal::v1::DetachReason reason) {
    if (!is_ready()) {
        return;
    }
    GatewayToZone m;
    m.mutable_session_detach()->set_gateway_session_id(session.ToWire());
    m.mutable_session_detach()->set_reason(reason);
    Send(m);
}

void ZoneLink::Forward(const common::InstanceId& session, const game::v1::ClientMessage& message) {
    if (!is_ready()) {
        return;
    }
    GatewayToZone m;
    m.mutable_session_input()->set_gateway_session_id(session.ToWire());
    *m.mutable_session_input()->mutable_message() = message;
    Send(m);
}

}  // namespace sa::gateway
