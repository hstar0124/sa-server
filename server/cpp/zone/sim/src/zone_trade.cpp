#include "sa/zone/sim/zone.hpp"

#include <utility>

namespace sa::zone::sim {

// ---- trading (ADR-0013) -------------------------------------------------------------------------
// The zone owns no trade state. It answers the questions only it can (is the counterparty a live
// player standing close enough, is the requester able to act) and hands the step to the host,
// which brings back the Backend's verdict through ResolveTrade.

void Zone::ApplyTrade(const TradeCommand& command) {
    const auto it = players_.find(command.player);
    if (it == players_.end()) {
        return;  // player left while the command was queued
    }
    Player& player = it->second;
    ++stats_.commands_applied;

    // A request without an id cannot be told apart from the next one; it is never remembered.
    if (command.request_id == common::RequestId{}) {
        Deliver(command.player,
                CommandResultEvent::Decided(command.request_id, RejectReason::kInvalidRequest));
        return;
    }
    // A retry of the step the Backend is deciding is answered from the player's state, so it is
    // never sent twice.
    if (player.trade_in_flight == command.request_id) {
        Deliver(command.player, CommandResultEvent::Pending(command.request_id));
        return;
    }
    if (const auto previous = player.recent_requests.Find(command.request_id)) {
        Deliver(command.player, *previous);
        return;
    }
    // One step at a time: the next one may depend on this one's verdict (an acceptance names the
    // version an offer produced). Not processed, so the client sends it again later.
    if (player.trade_in_flight) {
        Deliver(command.player, CommandResultEvent::Unknown(command.request_id));
        return;
    }

    const RejectReason reason = ValidateTrade(player, command);
    if (reason != RejectReason::kNone) {
        const auto result = CommandResultEvent::Decided(command.request_id, reason);
        player.recent_requests.Remember(result);
        Deliver(command.player, result);
        return;
    }

    player.trade_in_flight = command.request_id;
    Deliver(command.player, CommandResultEvent::Pending(command.request_id));
    TradeRequest request{.player = command.player,
                         .character = player.character,
                         .request_id = command.request_id,
                         .action = command.action,
                         .counterparty = {},
                         .trade = command.trade,
                         .items = command.items,
                         .gold = command.gold,
                         .version = command.version};
    if (command.action == TradeAction::kOpen) {
        request.counterparty = players_.at(command.target).character;
    }
    host_requests_.emplace_back(std::move(request));
    ++stats_.trade_steps_requested;
}

RejectReason Zone::ValidateTrade(const Player& player, const TradeCommand& command) const {
    if (player.departing) {
        return RejectReason::kTransferInProgress;
    }
    if (!player.vitals.IsAlive()) {
        return RejectReason::kDead;
    }
    if (command.action != TradeAction::kOpen) {
        return RejectReason::kNone;  // membership, versions and assets are the Backend's to judge
    }
    // A counterparty that is leaving through a portal is about to belong to another zone.
    const auto target = players_.find(command.target);
    if (target == players_.end() || command.target == command.player ||
        !target->second.vitals.IsAlive() || target->second.departing) {
        return RejectReason::kInvalidTarget;
    }
    if (!common::WithinDistance(player.position, target->second.position,
                                rules_.economy.trade_range_m)) {
        return RejectReason::kOutOfRange;
    }
    return RejectReason::kNone;
}

void Zone::ResolveTrade(const TradeRequest& request, RejectReason reason) {
    const auto it = players_.find(request.player);
    if (it == players_.end() || it->second.trade_in_flight != request.request_id) {
        return;  // the player left while the Backend was deciding
    }
    Player& player = it->second;
    player.trade_in_flight.reset();
    if (reason == RejectReason::kServiceUnavailable) {
        // Not remembered: a retry must reach the Backend again, whose request log makes it safe.
        Deliver(request.player, CommandResultEvent::Unknown(request.request_id));
        return;
    }
    const auto result = CommandResultEvent::Decided(request.request_id, reason);
    player.recent_requests.Remember(result);
    Deliver(request.player, result);
    if (reason == RejectReason::kNone) {
        ++stats_.trade_steps_accepted;
    }
}

}  // namespace sa::zone::sim
