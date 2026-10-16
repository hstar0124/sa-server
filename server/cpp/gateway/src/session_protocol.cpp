#include "session_protocol.hpp"

#include "sa/common/protocol_limits.hpp"

#include <utility>

namespace sa::gateway {

using game::v1::ClientMessage;
using game::v1::ServerMessage;

namespace {

constexpr std::uint64_t kRateWindowMs = 1000;

ServerMessage Pong(const game::v1::Ping& ping, std::uint64_t server_time_ms) {
    ServerMessage reply;
    auto* pong = reply.mutable_pong();
    pong->set_nonce(ping.nonce());
    pong->set_client_time_ms(ping.client_time_ms());
    pong->set_server_time_ms(server_time_ms);
    return reply;
}

game::v1::KickReason KickFor(backend::v1::RedeemOutcome outcome) {
    switch (outcome) {
        case backend::v1::REDEEM_OUTCOME_ACTIVE_SESSION_EXISTS:
            return game::v1::KICK_REASON_DUPLICATE_SESSION;
        case backend::v1::REDEEM_OUTCOME_NO_ZONE_AVAILABLE:
            return game::v1::KICK_REASON_ZONE_UNAVAILABLE;
        case backend::v1::REDEEM_OUTCOME_UNKNOWN_GATEWAY:
            return game::v1::KICK_REASON_SERVICE_UNAVAILABLE;
        default:
            return game::v1::KICK_REASON_INVALID_TICKET;
    }
}

}  // namespace

SessionProtocol::SessionProtocol(const EntryTicketVerifier& verifier, SessionProtocolConfig config,
                                 common::InstanceId session_id)
    : verifier_(verifier), config_(config), session_id_(session_id) {}

// ---- client -> gateway ----------------------------------------------------------------------

SessionProtocol::Reaction SessionProtocol::OnClientMessage(const ClientMessage& message,
                                                           std::uint64_t now_ms) {
    if (state_ == State::kClosed) {
        return Reaction{.close = true};
    }
    if (!WithinRateLimit(now_ms)) {
        return Kick(game::v1::KICK_REASON_RATE_LIMITED);
    }
    switch (message.body_case()) {
        case ClientMessage::kPing:
            return Reaction{.replies = {Pong(message.ping(), now_ms)}};
        case ClientMessage::kHello:
            if (state_ != State::kAwaitingHello) {
                return Kick(game::v1::KICK_REASON_PROTOCOL_ERROR);
            }
            return OnHello(message.hello(), now_ms);
        case ClientMessage::kMoveInput:
        case ClientMessage::kCastSkill:
        case ClientMessage::kPickupItem:
        case ClientMessage::kChangeZone:
            if (state_ != State::kInZone) {
                return Kick(game::v1::KICK_REASON_PROTOCOL_ERROR);
            }
            return Reaction{.effects = {ForwardToZone{.message = message}}};
        case ClientMessage::BODY_NOT_SET:
            break;
    }
    return Kick(game::v1::KICK_REASON_PROTOCOL_ERROR);
}

SessionProtocol::Reaction SessionProtocol::OnHello(const game::v1::Hello& hello,
                                                   std::uint64_t now_ms) {
    if (hello.protocol_version() != common::protocol::kCurrentVersion) {
        return Kick(game::v1::KICK_REASON_PROTOCOL_VERSION);
    }
    auto ticket = verifier_.Verify(std::as_bytes(std::span{hello.entry_ticket()}), now_ms);
    if (!ticket) {
        return Kick(game::v1::KICK_REASON_INVALID_TICKET);
    }
    ticket_ = *ticket;
    state_ = State::kRedeeming;
    return Reaction{.effects = {RedeemTicket{.ticket = *ticket}}};
}

bool SessionProtocol::WithinRateLimit(std::uint64_t now_ms) {
    if (now_ms - window_start_ms_ >= kRateWindowMs) {
        window_start_ms_ = now_ms;
        messages_in_window_ = 0;
    }
    return ++messages_in_window_ <= config_.max_messages_per_second;
}

// ---- backend ----------------------------------------------------------------------------------

SessionProtocol::Reaction SessionProtocol::OnRedeemResult(
    const backend::v1::RedeemEntryTicketResponse& response) {
    if (state_ != State::kRedeeming || !ticket_) {
        return {};
    }
    if (response.outcome() != backend::v1::REDEEM_OUTCOME_GRANTED) {
        return Kick(KickFor(response.outcome()));
    }
    session_granted_ = true;
    zone_ = common::ZoneId{response.zone().zone_id()};
    state_ = State::kAttaching;

    internal::v1::SessionAttach attach;
    attach.set_gateway_session_id(session_id_.ToWire());
    attach.set_account_id(ticket_->account.value());
    attach.set_character_id(response.character().character_id());
    attach.set_character_name(response.character().name());
    attach.set_class_id(response.character().class_id());
    attach.set_owner_epoch(response.owner_epoch());
    return Reaction{.effects = {AttachToZone{.zone = *zone_, .attach = std::move(attach)}}};
}

SessionProtocol::Reaction SessionProtocol::OnRedeemFailed() {
    if (state_ != State::kRedeeming) {
        return {};
    }
    return Kick(game::v1::KICK_REASON_SERVICE_UNAVAILABLE);
}

// ---- zone -------------------------------------------------------------------------------------

SessionProtocol::Reaction SessionProtocol::OnZoneAttached(common::EntityId /*entity*/) {
    if (state_ != State::kAttaching) {
        return {};
    }
    state_ = State::kInZone;
    return {};
}

SessionProtocol::Reaction SessionProtocol::OnZoneRejected(internal::v1::RejectReason /*reason*/) {
    if (state_ != State::kAttaching) {
        return {};
    }
    // Nothing to detach: the zone never admitted us. The Backend session must still end.
    zone_.reset();
    return Kick(game::v1::KICK_REASON_ZONE_UNAVAILABLE);
}

SessionProtocol::Reaction SessionProtocol::OnZoneEvicted(internal::v1::EvictReason /*reason*/) {
    if (state_ != State::kInZone && state_ != State::kAttaching) {
        return {};
    }
    // The zone already dropped us; no detach is owed.
    zone_.reset();
    return Kick(game::v1::KICK_REASON_ZONE_UNAVAILABLE);
}

SessionProtocol::Reaction SessionProtocol::OnZoneLinkDown() {
    if (state_ != State::kInZone && state_ != State::kAttaching) {
        return {};
    }
    zone_.reset();
    return Kick(game::v1::KICK_REASON_SERVICE_UNAVAILABLE);
}

SessionProtocol::Reaction SessionProtocol::OnZoneOutput(const ServerMessage& message) {
    if (state_ != State::kInZone) {
        return {};
    }
    return Reaction{.replies = {message}};
}

// ---- lifecycle --------------------------------------------------------------------------------

SessionProtocol::Reaction SessionProtocol::OnClientDisconnected() {
    Reaction reaction;
    if (state_ != State::kClosed) {
        Teardown(reaction, internal::v1::DETACH_REASON_CLIENT_DISCONNECTED, "client_disconnected");
    }
    return reaction;
}

SessionProtocol::Reaction SessionProtocol::Kick(game::v1::KickReason reason) {
    Reaction reaction;
    ServerMessage reply;
    reply.mutable_kick()->set_reason(reason);
    reaction.replies.push_back(std::move(reply));
    reaction.close = true;
    Teardown(reaction, internal::v1::DETACH_REASON_PROTOCOL_ERROR,
             "kick:" + game::v1::KickReason_Name(reason));
    return reaction;
}

void SessionProtocol::Teardown(Reaction& reaction, internal::v1::DetachReason detach_reason,
                               std::string end_reason) {
    if (zone_ && (state_ == State::kInZone || state_ == State::kAttaching)) {
        reaction.effects.emplace_back(DetachFromZone{.zone = *zone_, .reason = detach_reason});
    }
    if (session_granted_) {
        reaction.effects.emplace_back(EndSession{.reason = std::move(end_reason)});
        session_granted_ = false;
    }
    zone_.reset();
    state_ = State::kClosed;
}

}  // namespace sa::gateway
