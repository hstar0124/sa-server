#include "session_protocol.hpp"

#include "sa/common/protocol_limits.hpp"

#include <utility>

namespace sa::gateway {

using game::v1::ClientMessage;
using game::v1::ServerMessage;

namespace {

// The window the rate limit counts in: SessionProtocolConfig::max_messages_per_second is a count
// per second by definition.
constexpr std::chrono::seconds kRateWindow{1};

ServerMessage Pong(const game::v1::Ping& ping, common::WallTime server_time) {
    ServerMessage reply;
    auto* pong = reply.mutable_pong();
    pong->set_nonce(ping.nonce());
    pong->set_client_time_ms(ping.client_time_ms());
    pong->set_server_time_ms(common::ToUnixMillis(server_time));
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
                                                           ArrivalTime now) {
    if (state_ == State::kClosed) {
        return Reaction{.close = true};
    }
    if (!WithinRateLimit(now.monotonic)) {
        return Kick(game::v1::KICK_REASON_RATE_LIMITED);
    }
    switch (message.body_case()) {
        case ClientMessage::kPing:
            return Reaction{.replies = {Pong(message.ping(), now.wall)}};
        case ClientMessage::kHello:
            if (state_ != State::kAwaitingHello) {
                return Kick(game::v1::KICK_REASON_PROTOCOL_ERROR);
            }
            return OnHello(message.hello(), now.wall);
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
                                                   common::WallTime now) {
    if (hello.protocol_version() != common::protocol::kCurrentVersion) {
        return Kick(game::v1::KICK_REASON_PROTOCOL_VERSION);
    }
    auto ticket = verifier_.Verify(std::as_bytes(std::span{hello.entry_ticket()}), now);
    if (!ticket) {
        return Kick(game::v1::KICK_REASON_INVALID_TICKET);
    }
    ticket_ = *ticket;
    state_ = State::kRedeeming;
    redeem_in_flight_ = true;
    return Reaction{.effects = {RedeemTicket{.ticket = *ticket}}};
}

bool SessionProtocol::WithinRateLimit(std::chrono::steady_clock::time_point now) {
    if (now - window_start_ >= kRateWindow) {
        window_start_ = now;
        messages_in_window_ = 0;
    }
    return ++messages_in_window_ <= config_.max_messages_per_second;
}

// ---- backend ----------------------------------------------------------------------------------

SessionProtocol::Reaction SessionProtocol::OnRedeemResult(
    const backend::v1::RedeemEntryTicketResponse& response) {
    if (!std::exchange(redeem_in_flight_, false)) {
        return {};
    }
    const bool granted = response.outcome() == backend::v1::REDEEM_OUTCOME_GRANTED;
    if (state_ != State::kRedeeming || !ticket_) {
        // The client left while the Backend decided. A granted session would otherwise stay live
        // and refuse this character everywhere until this gateway's lease ends.
        return granted ? Reaction{.effects = {EndSession{.reason = "closed_during_redeem"}}}
                       : Reaction{};
    }
    if (!granted) {
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
    if (!std::exchange(redeem_in_flight_, false)) {
        return {};
    }
    // The Backend may have committed before the deadline passed. Ending a session that was never
    // created is harmless, so end it either way.
    Reaction reaction =
        state_ == State::kRedeeming ? Kick(game::v1::KICK_REASON_SERVICE_UNAVAILABLE) : Reaction{};
    reaction.effects.emplace_back(EndSession{.reason = "redeem_outcome_unknown"});
    return reaction;
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

SessionProtocol::Reaction SessionProtocol::OnHelloDeadline() {
    if (state_ != State::kAwaitingHello) {
        return {};
    }
    return Kick(game::v1::KICK_REASON_PROTOCOL_ERROR);
}

SessionProtocol::Reaction SessionProtocol::OnServerClose(game::v1::KickReason reason) {
    if (state_ == State::kClosed) {
        return {};
    }
    return Kick(reason, internal::v1::DETACH_REASON_GATEWAY_SHUTDOWN);
}

SessionProtocol::Reaction SessionProtocol::Kick(game::v1::KickReason reason,
                                                internal::v1::DetachReason detach_reason) {
    Reaction reaction;
    ServerMessage reply;
    reply.mutable_kick()->set_reason(reason);
    reaction.replies.push_back(std::move(reply));
    reaction.close = true;
    Teardown(reaction, detach_reason, "kick:" + game::v1::KickReason_Name(reason));
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
