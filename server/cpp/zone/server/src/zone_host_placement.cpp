// Where characters are and where they go (ADR-0010): periodic checkpoints, and handing a
// character to another zone once the Backend has moved its ownership.
#include "codec.hpp"
#include "zone_host.hpp"

#include <spdlog/spdlog.h>

namespace sa::zone {

using internal::v1::ZoneToGateway;

std::optional<common::InstanceId> ZoneHost::SessionOfCharacter(
    common::CharacterId character) const {
    for (const auto& [id, session] : sessions_) {
        if (session.character == character) {
            return id;
        }
    }
    return std::nullopt;
}

// ---- checkpoints -------------------------------------------------------------------------------

void ZoneHost::SaveCheckpoints() {
    for (auto& [id, session] : sessions_) {
        // A departing character's position is the Backend's to set: the transfer stores the
        // arrival point under the new generation.
        if (zone_.IsDeparting(session.entity)) {
            continue;
        }
        if (session.checkpointed && session.checkpointed == zone_.PositionOf(session.entity)) {
            continue;  // standing still since the last save
        }
        SaveCheckpoint(id, session);
    }
}

void ZoneHost::SaveCheckpoint(const common::InstanceId& session_id, Session& session) {
    const auto position = zone_.PositionOf(session.entity);
    if (!position) {
        return;
    }
    session.checkpointed = *position;
    const codec::CheckpointInfo checkpoint{.world = zone_.config().world_id,
                                           .character = session.character,
                                           .zone = zone_.config().zone_id,
                                           .position = *position,
                                           .version = ++session.checkpoint_version};
    directory_.SaveCheckpoint(
        codec::ToSaveCheckpointRequest(checkpoint, AuthorityFor(session)),
        [this, session_id, entity = session.entity](
            const std::optional<backend::v1::SaveCheckpointResponse>& response) {
            OnCheckpointSaved(session_id, entity, response);
        });
}

void ZoneHost::OnCheckpointSaved(
    const common::InstanceId& session_id, common::EntityId entity,
    const std::optional<backend::v1::SaveCheckpointResponse>& response) {
    const auto it = sessions_.find(session_id);
    Session* session = it != sessions_.end() && it->second.entity == entity ? &it->second : nullptr;
    const auto outcome =
        response ? response->outcome() : backend::v1::CHECKPOINT_OUTCOME_UNSPECIFIED;
    switch (outcome) {
        case backend::v1::CHECKPOINT_OUTCOME_SAVED:
            ++stats_.checkpoints_saved;
            return;
        case backend::v1::CHECKPOINT_OUTCOME_STALE_VERSION:
            return;  // a later save of this session landed first; nothing is lost
        case backend::v1::CHECKPOINT_OUTCOME_STALE_AUTHORITY:
            ++stats_.checkpoints_refused;
            // While a departure is open its own answer settles the session; otherwise another
            // zone owns the character and nothing this session does here can be kept.
            if (session != nullptr && !zone_.IsDeparting(entity)) {
                EvictNotOwner(session_id);
            }
            return;
        case backend::v1::CHECKPOINT_OUTCOME_UNSPECIFIED:
        default:
            ++stats_.backend_failures;
            if (session != nullptr) {
                session->checkpointed.reset();  // send it again at the next cadence
            }
            return;
    }
}

// ---- transfers ---------------------------------------------------------------------------------

void ZoneHost::Transfer(const sim::ZoneTransferRequest& transfer) {
    const Session* session = SessionOf(transfer.player);
    if (session == nullptr) {
        zone_.FailTransfer(transfer, sim::TransferOutcome::kUnavailable);
        return;
    }
    if (session->trading) {
        // The Backend cancels a character's trades only for its owner, so the cancel goes first:
        // once ownership moved, this zone could no longer close the trade (ADR-0013).
        CancelTradesOf(*session, [this, transfer] { RequestTransfer(transfer); });
        return;
    }
    RequestTransfer(transfer);
}

void ZoneHost::RequestTransfer(const sim::ZoneTransferRequest& transfer) {
    const Session* session = SessionOf(transfer.player);
    if (session == nullptr) {
        zone_.FailTransfer(transfer, sim::TransferOutcome::kUnavailable);
        return;
    }
    directory_.TransferCharacter(
        codec::ToTransferRequest(transfer, zone_.config().world_id, AuthorityFor(*session)),
        [this, transfer](const std::optional<backend::v1::TransferCharacterResponse>& response) {
            OnTransferDecided(transfer, response);
        });
}

void ZoneHost::OnTransferDecided(
    const sim::ZoneTransferRequest& transfer,
    const std::optional<backend::v1::TransferCharacterResponse>& response) {
    if (!response) {
        // The Backend may have committed. The player stays and may retry: the retry carries the
        // same transfer id, so it learns the stored outcome instead of moving the character twice.
        ++stats_.backend_failures;
        zone_.FailTransfer(transfer, sim::TransferOutcome::kUnavailable);
        return;
    }
    const sim::TransferOutcome outcome = codec::ToTransferOutcome(response->outcome());
    if (outcome == sim::TransferOutcome::kTransferred) {
        HandOver(transfer, *response);
        return;
    }
    ++stats_.transfers_refused;
    spdlog::info("transfer of character {} to zone {} refused: {}", transfer.character.value(),
                 transfer.to_zone.value(), backend::v1::TransferOutcome_Name(response->outcome()));
    zone_.FailTransfer(transfer, outcome);
    if (outcome == sim::TransferOutcome::kNotOwner) {
        if (const auto by_entity = session_by_entity_.find(transfer.player);
            by_entity != session_by_entity_.end()) {
            EvictNotOwner(by_entity->second);
        }
    }
}

void ZoneHost::HandOver(const sim::ZoneTransferRequest& transfer,
                        const backend::v1::TransferCharacterResponse& response) {
    const auto by_entity = session_by_entity_.find(transfer.player);
    if (by_entity == session_by_entity_.end()) {
        // The client left while the Backend was deciding; the character now lives in the
        // destination and the next entry places it there.
        (void)zone_.CompleteTransfer(transfer);
        return;
    }
    const common::InstanceId session_id = by_entity->second;
    const Session session = sessions_.at(session_id);
    // The gateway follows only a newer generation in another zone; a reply that is neither would
    // leave the session nowhere. The ownership is elsewhere either way, so the session ends.
    if (response.to_zone().zone_id() != transfer.to_zone.value() ||
        response.owner_epoch() <= session.epoch.value()) {
        spdlog::error(
            "transfer of character {}: Backend named zone {} epoch {}, asked zone {} "
            "above epoch {}",
            transfer.character.value(), response.to_zone().zone_id(), response.owner_epoch(),
            transfer.to_zone.value(), session.epoch.value());
        zone_.FailTransfer(transfer, sim::TransferOutcome::kNotOwner);
        EvictNotOwner(session_id);
        return;
    }
    const auto result = zone_.CompleteTransfer(transfer);
    if (!result) {
        return;  // not departing under this request any more
    }
    // Ownership has moved: finish the command, release the character here and tell the gateway
    // where to attach the session next. The destination admits it under the new generation.
    game::v1::ServerMessage message;
    *message.mutable_command_result() = codec::ToCommandResult(*result);
    SendToSession(session, session_id, message);
    ZoneToGateway handover;
    *handover.mutable_session_transfer() =
        codec::ToSessionTransfer(session_id, response, transfer.arrival);
    session.sink->Send(handover);
    RemoveSession(session_id, false);  // CompleteTransfer already despawned the player
    ++stats_.transfers_completed;
    spdlog::debug("character {} handed to zone {} epoch {}", transfer.character.value(),
                  response.to_zone().zone_id(), response.owner_epoch());
}

}  // namespace sa::zone
