#include "trade_codec.hpp"
#include "zone_host.hpp"

#include <spdlog/spdlog.h>

#include <utility>

namespace sa::zone {

// ---- trades (ADR-0013) --------------------------------------------------------------------------
// The Backend owns the negotiation. The host carries each validated step there, brings the verdict
// back to the requester through the simulation, and relays the trade's new state to both
// participants that are in this zone.

void ZoneHost::Trade(const sim::TradeRequest& request) {
    Session* session = SessionOf(request.player);
    if (session == nullptr) {
        return;  // the player left this tick; the Backend's expiry closes whatever was open
    }
    // Until a TradeUpdate says the trade is closed, a session that ends asks for a cancel. Set on
    // every step, so a step whose answer was lost still leads to one.
    session->trading = true;
    const common::WorldId world = zone_.config().world_id;
    const codec::ZoneAuthority authority = AuthorityFor(*session);
    auto reply = [this, request](const std::optional<backend::v1::TradeResponse>& response) {
        OnTradeDecided(request, response);
    };
    ++stats_.trade_steps_sent;
    switch (request.action) {
        case sim::TradeAction::kOpen:
            trades_.OpenTrade(codec::ToOpenTradeRequest(request, world, authority),
                              std::move(reply));
            return;
        case sim::TradeAction::kOffer:
            trades_.OfferTrade(codec::ToOfferTradeRequest(request, world, authority),
                               std::move(reply));
            return;
        case sim::TradeAction::kAccept:
            trades_.AcceptTrade(codec::ToAcceptTradeRequest(request, world, authority),
                                std::move(reply));
            return;
        case sim::TradeAction::kCancel:
            trades_.CancelTrade(codec::ToCancelTradeRequest(request, world, authority),
                                std::move(reply));
            return;
    }
}

void ZoneHost::OnTradeDecided(const sim::TradeRequest& request,
                              const std::optional<backend::v1::TradeResponse>& response) {
    if (!response) {
        // The Backend may have applied it. The retry carries the same key and gets that answer.
        ++stats_.backend_failures;
        zone_.ResolveTrade(request, sim::RejectReason::kServiceUnavailable);
        return;
    }
    const sim::RejectReason reason = codec::ToTradeReject(response->outcome());
    if (reason != sim::RejectReason::kNone) {
        ++stats_.trade_steps_refused;
    }
    zone_.ResolveTrade(request, reason);
    if (response->has_trade()) {
        RelayTradeState(response->trade());
    }
    if (response->outcome() == backend::v1::TRADE_OUTCOME_SETTLED) {
        ++stats_.trades_settled;
    }
    if (reason == sim::RejectReason::kNotOwner) {
        if (const auto it = session_by_entity_.find(request.player);
            it != session_by_entity_.end()) {
            EvictNotOwner(it->second);
        }
    }
}

// Both participants that are in this zone learn the new state. On settlement both get their
// assets re-read, so the InventoryUpdate carries the full post-trade inventory and wallet.
void ZoneHost::RelayTradeState(const backend::v1::TradeState& state) {
    const auto entity_of = [this](common::CharacterId character) {
        const auto session_id = SessionOfCharacter(character);
        return session_id ? sessions_.at(*session_id).entity : common::EntityId{};
    };
    const bool open = state.status() == backend::v1::TRADE_STATUS_OPEN;
    for (const backend::v1::TradeSide* side : {&state.initiator(), &state.counterparty()}) {
        const common::CharacterId character{side->character_id()};
        const auto session_id = SessionOfCharacter(character);
        if (!session_id) {
            continue;  // not in this zone
        }
        Session& session = sessions_.at(*session_id);
        session.trading = open;
        game::v1::ServerMessage message;
        *message.mutable_trade_update() = codec::ToTradeUpdate(state, character, entity_of);
        SendToSession(session, *session_id, message);
        if (state.status() == backend::v1::TRADE_STATUS_COMPLETED) {
            RequestAssets(*session_id);
        }
    }
}

void ZoneHost::CancelTradesOf(const Session& session, std::function<void()> then) {
    ++stats_.trade_cancels_on_leave;
    // No client request names this cancel. A fresh key, kept by every retry of the call, keeps it
    // apart from any earlier cancel of the same character in the same ownership generation.
    trades_.CancelTrade(
        codec::ToLeaveCancelRequest(session.character, zone_.config().world_id,
                                    AuthorityFor(session), common::RequestKey::Generate()),
        [this, character = session.character,
         then = std::move(then)](const std::optional<backend::v1::TradeResponse>& response) {
            if (!response) {
                spdlog::warn("trades of character {} not cancelled; the Backend's expiry will",
                             character.value());
            } else if (response->has_trade()) {
                RelayTradeState(response->trade());  // the counterparty learns the trade is gone
            }
            if (then) {
                then();
            }
        });
}

}  // namespace sa::zone
