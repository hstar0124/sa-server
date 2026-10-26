#include "client_session.hpp"

#include "sa/common/wall_clock.hpp"

#include <spdlog/spdlog.h>

#include <utility>

namespace sa::gateway {
namespace {

ArrivalTime Now() {
    return ArrivalTime{.monotonic = std::chrono::steady_clock::now(), .wall = common::WallNow()};
}

}  // namespace

ClientSession::ClientSession(std::shared_ptr<common::MessageConnection> connection,
                             Dependencies deps)
    : id_(common::InstanceId::Generate()),
      connection_(std::move(connection)),
      deps_(deps),
      protocol_(deps_.verifier, deps_.protocol, id_),
      hello_deadline_(deps.io),
      idle_timer_(deps.io) {}

void ClientSession::Start() {
    deps_.registry.Add(id_, weak_from_this());
    spdlog::debug("session {} open peer={}", id_.ToHex(), connection_->PeerAddress());
    connection_->Start(common::MessageConnection::Callbacks{
        .on_message = [self =
                           shared_from_this()](std::span<const std::byte> p) { self->OnFrame(p); },
        .on_closed = [self = shared_from_this()](const std::error_code& ec) { self->OnClosed(ec); },
    });
    hello_deadline_.expires_after(deps_.protocol.hello_timeout);
    hello_deadline_.async_wait([self = shared_from_this()](const std::error_code& ec) {
        if (!ec) {
            self->Apply(self->protocol_.OnHelloDeadline());
        }
    });
    ScheduleIdleCheck(std::chrono::steady_clock::now() + deps_.protocol.idle_timeout);
}

void ClientSession::ScheduleIdleCheck(std::chrono::steady_clock::time_point at) {
    idle_timer_.expires_at(at);
    idle_timer_.async_wait([self = shared_from_this()](const std::error_code& ec) {
        if (ec || self->finished_) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        self->Apply(self->protocol_.OnIdleCheck(now));
        if (self->protocol_.state() != SessionProtocol::State::kClosed) {
            self->ScheduleIdleCheck(
                self->protocol_.IdleDeadline().value_or(now + self->deps_.protocol.idle_timeout));
        }
    });
}

void ClientSession::Shutdown(game::v1::KickReason reason) {
    Apply(protocol_.OnServerClose(reason));
}

// ---- inbound ------------------------------------------------------------------------------------

void ClientSession::OnFrame(std::span<const std::byte> payload) {
    game::v1::ClientMessage message;
    if (!message.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        const game::v1::ClientMessage
            malformed;  // an empty body is a protocol error in the state machine
        Apply(protocol_.OnClientMessage(malformed, Now()));
        return;
    }
    Apply(protocol_.OnClientMessage(message, Now()));
}

void ClientSession::OnClosed(const std::error_code& reason) {
    if (finished_) {
        return;
    }
    finished_ = true;
    hello_deadline_.cancel();
    idle_timer_.cancel();
    spdlog::debug("session {} closed ({})", id_.ToHex(), reason ? reason.message() : "orderly");
    Apply(protocol_.OnClientDisconnected());
    deps_.registry.Remove(id_);
    if (on_finished_) {
        on_finished_();
    }
}

void ClientSession::OnZoneAttached(common::ZoneId zone,
                                   const internal::v1::SessionAttached& attached) {
    Apply(protocol_.OnZoneAttached(zone, common::EntityId{attached.entity_id()}));
}
void ClientSession::OnZoneRejected(common::ZoneId zone,
                                   const internal::v1::SessionRejected& rejected) {
    spdlog::info("session {} rejected by zone {}: {}", id_.ToHex(), zone.value(),
                 internal::v1::RejectReason_Name(rejected.reason()));
    Apply(protocol_.OnZoneRejected(zone, rejected.reason()));
}
void ClientSession::OnZoneEvicted(common::ZoneId zone,
                                  const internal::v1::SessionEvicted& evicted) {
    Apply(protocol_.OnZoneEvicted(zone, evicted.reason()));
}
void ClientSession::OnZoneTransfer(common::ZoneId zone,
                                   const internal::v1::SessionTransfer& transfer) {
    spdlog::info("session {} moving from zone {} to zone {} (epoch {})", id_.ToHex(), zone.value(),
                 transfer.to_zone_id(), transfer.owner_epoch());
    Apply(protocol_.OnZoneTransfer(zone, transfer));
}
void ClientSession::OnZoneOutput(common::ZoneId zone, const game::v1::ServerMessage& message) {
    Apply(protocol_.OnZoneOutput(zone, message, connection_->QueuedBytes()));
}
void ClientSession::OnZoneLinkDown(common::ZoneId zone) {
    if (protocol_.zone() == zone) {
        Apply(protocol_.OnZoneLinkDown());
    }
}

// ---- effects
//
// Apply() -> Perform(AttachToZone) -> Apply() is a bounded chain: the inner reaction comes from a
// protocol transition into Closed, which produces no further attach.
// NOLINTBEGIN(misc-no-recursion)
// -------------------------------------------------------------------------------------

void ClientSession::Apply(SessionProtocol::Reaction reaction) {
    for (const game::v1::ServerMessage& reply : reaction.replies) {
        // A closing connection drops what it cannot send anymore; anything else is a bug worth
        // seeing (a reply larger than the client frame limit).
        if (const auto sent = connection_->Send(reply);
            !sent && sent.error() != common::SendError::kClosed) {
            spdlog::warn("session {}: reply dropped: {}", id_.ToHex(),
                         common::Describe(sent.error()));
        }
    }
    for (SessionProtocol::Effect& effect : reaction.effects) {
        std::visit([this](auto& e) { Perform(std::move(e)); }, effect);
    }
    if (reaction.close) {
        connection_->Close();
    }
}

void ClientSession::Perform(SessionProtocol::RedeemTicket effect) {
    backend::v1::RedeemEntryTicketRequest request;
    request.set_ticket_id(effect.ticket.ticket_id.ToWire());
    request.set_account_id(effect.ticket.account.value());
    request.set_character_id(effect.ticket.character.value());
    request.set_world_id(effect.ticket.world.value());
    request.set_gateway_session_id(id_.ToWire());
    // The answer is applied even after the client left: a late grant must still be ended.
    deps_.directory.Redeem(
        std::move(request), [self = shared_from_this()](
                                std::optional<backend::v1::RedeemEntryTicketResponse> response) {
            if (!response) {
                self->Apply(self->protocol_.OnRedeemFailed());
                return;
            }
            if (response->outcome() != backend::v1::REDEEM_OUTCOME_GRANTED) {
                spdlog::info("session {} redeem refused: {}", self->id_.ToHex(),
                             backend::v1::RedeemOutcome_Name(response->outcome()));
            }
            self->Apply(self->protocol_.OnRedeemResult(*response));
        });
}

void ClientSession::Perform(const SessionProtocol::AttachToZone& effect) {
    ZoneLink* link = deps_.zones.Find(effect.zone);
    if (link == nullptr || !link->is_ready()) {
        spdlog::warn("session {} assigned to zone {} which has no ready link", id_.ToHex(),
                     effect.zone.value());
        Apply(protocol_.OnZoneLinkDown());
        return;
    }
    link->Attach(effect.attach);
}

void ClientSession::Perform(const SessionProtocol::ForwardToZone& effect) {
    if (const auto zone = protocol_.zone()) {
        if (ZoneLink* link = deps_.zones.Find(*zone)) {
            link->Forward(id_, effect.message);
        }
    }
}

void ClientSession::Perform(const SessionProtocol::DetachFromZone& effect) {
    if (ZoneLink* link = deps_.zones.Find(effect.zone)) {
        link->Detach(id_, effect.reason);
    }
}

// NOLINTEND(misc-no-recursion)

void ClientSession::Perform(SessionProtocol::EndSession effect) {
    deps_.directory.EndSession(id_, std::move(effect.reason));
}

}  // namespace sa::gateway
