// Leaving through a portal (ADR-0010). The zone validates and freezes; the Backend moves the
// ownership; the host brings the answer back through CompleteTransfer/FailTransfer.
#include "sa/zone/sim/zone.hpp"

#include <format>

namespace sa::zone::sim {

std::expected<void, std::string> Zone::ValidatePortalsLeaveZone() const {
    // The map does not know which zone serves it; a portal into its own zone would ask the
    // Backend for a transfer it always refuses.
    for (const Portal& portal : map_.portals) {
        if (portal.to_zone == config_.zone_id) {
            return std::unexpected(std::format("portal {} leads back into zone {}",
                                               portal.id.value(), config_.zone_id.value()));
        }
    }
    return {};
}

void Zone::ApplyChangeZone(const ChangeZoneCommand& command) {
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
    // A retry of the departure still waiting for the Backend is answered from the player's state,
    // so it is never sent twice.
    if (player.departing == command.request_id) {
        Deliver(command.player, CommandResultEvent::Pending(command.request_id));
        return;
    }
    if (const auto previous = player.recent_requests.Find(command.request_id)) {
        Deliver(command.player, *previous);
        return;
    }

    const Portal* portal = map_.FindPortal(command.portal);
    const RejectReason reason = ValidateChangeZone(player, portal);
    if (reason != RejectReason::kNone) {
        const auto result = CommandResultEvent::Decided(command.request_id, reason);
        player.recent_requests.Remember(result);
        Deliver(command.player, result);
        return;
    }

    // Frozen from here: no movement, no casts, no pickups until the Backend has decided.
    player.departing = command.request_id;
    player.desired_direction = {};
    SetVelocity(player.velocity, player.changed_at, {});
    player.cast.reset();
    Deliver(command.player, CommandResultEvent::Pending(command.request_id));
    host_requests_.emplace_back(ZoneTransferRequest{.player = command.player,
                                                    .character = player.character,
                                                    .request_id = command.request_id,
                                                    .portal = portal->id,
                                                    .to_zone = portal->to_zone,
                                                    .arrival = portal->arrival});
    ++stats_.transfers_requested;
}

RejectReason Zone::ValidateChangeZone(const Player& player, const Portal* portal) {
    if (player.departing) {
        return RejectReason::kTransferInProgress;
    }
    if (!player.vitals.IsAlive()) {
        return RejectReason::kDead;
    }
    if (portal == nullptr) {
        return RejectReason::kUnknownPortal;
    }
    if (!common::WithinDistance(player.position, portal->position, portal->radius_m)) {
        return RejectReason::kOutOfPortalRange;
    }
    return RejectReason::kNone;
}

bool Zone::IsDeparting(common::EntityId player) const {
    const auto it = players_.find(player);
    return it != players_.end() && it->second.departing.has_value();
}

Zone::Player* Zone::DepartingPlayer(const ZoneTransferRequest& transfer) {
    const auto it = players_.find(transfer.player);
    if (it == players_.end() || it->second.departing != transfer.request_id) {
        return nullptr;
    }
    return &it->second;
}

std::optional<CommandResultEvent> Zone::CompleteTransfer(const ZoneTransferRequest& transfer) {
    if (DepartingPlayer(transfer) == nullptr) {
        return std::nullopt;
    }
    DespawnPlayer(transfer.player);
    ++stats_.transfers_completed;
    return CommandResultEvent::Decided(transfer.request_id, RejectReason::kNone);
}

void Zone::FailTransfer(const ZoneTransferRequest& transfer, TransferOutcome outcome) {
    Player* player = DepartingPlayer(transfer);
    if (player == nullptr) {
        return;
    }
    CommandResultEvent result = CommandResultEvent::Unknown(transfer.request_id);
    switch (outcome) {
        case TransferOutcome::kTransferred:
            return;  // not a failure: CompleteTransfer settles it
        case TransferOutcome::kZoneUnavailable:
            result =
                CommandResultEvent::Decided(transfer.request_id, RejectReason::kZoneUnavailable);
            break;
        case TransferOutcome::kNotOwner:
            result = CommandResultEvent::Decided(transfer.request_id, RejectReason::kNotOwner);
            break;
        case TransferOutcome::kUnavailable:
            break;  // not remembered: the retry with the same request id runs again
    }
    player->departing.reset();
    if (result.status != CommandStatus::kUnknown) {
        player->recent_requests.Remember(result);
    }
    Deliver(transfer.player, result);
}

}  // namespace sa::zone::sim
