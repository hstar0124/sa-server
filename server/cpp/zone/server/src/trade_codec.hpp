#pragma once

#include "sa/common/ids.hpp"
#include "sa/zone/sim/entities.hpp"
#include "sa/zone/sim/events.hpp"

#include "codec.hpp"

#include <backend/trade.pb.h>
#include <game/client_messages.pb.h>
#include <game/server_messages.pb.h>

#include <expected>
#include <functional>

// Trade steps (ADR-0013) between the client's messages, the simulation's commands and the
// Backend's Trade service. Pure functions, like the rest of the codec.
namespace sa::zone::codec {

// ---- client -> zone -----------------------------------------------------------------------------
// A trade or item id that is not 16 bytes is refused here and never reaches the simulation.
[[nodiscard]] std::expected<sim::TradeCommand, InputError> ToTradeCommand(
    common::EntityId player, const game::v1::TradeOpen& open);
[[nodiscard]] std::expected<sim::TradeCommand, InputError> ToTradeCommand(
    common::EntityId player, const game::v1::TradeOffer& offer);
[[nodiscard]] std::expected<sim::TradeCommand, InputError> ToTradeCommand(
    common::EntityId player, const game::v1::TradeAccept& accept);
[[nodiscard]] std::expected<sim::TradeCommand, InputError> ToTradeCommand(
    common::EntityId player, const game::v1::TradeCancel& cancel);

// ---- zone -> Backend ----------------------------------------------------------------------------
// The Backend's idempotency key of a client's trade step: CommandDigest under its own domain.
[[nodiscard]] common::RequestKey TradeRequestKey(const sim::TradeRequest& request,
                                                 common::OwnerEpoch epoch);
[[nodiscard]] backend::v1::OpenTradeRequest ToOpenTradeRequest(const sim::TradeRequest& request,
                                                               common::WorldId world,
                                                               const ZoneAuthority& authority);
[[nodiscard]] backend::v1::OfferTradeRequest ToOfferTradeRequest(const sim::TradeRequest& request,
                                                                 common::WorldId world,
                                                                 const ZoneAuthority& authority);
[[nodiscard]] backend::v1::AcceptTradeRequest ToAcceptTradeRequest(const sim::TradeRequest& request,
                                                                   common::WorldId world,
                                                                   const ZoneAuthority& authority);
[[nodiscard]] backend::v1::CancelTradeRequest ToCancelTradeRequest(const sim::TradeRequest& request,
                                                                   common::WorldId world,
                                                                   const ZoneAuthority& authority);
// Cancels every open trade of a character whose session ended. No client request names it, so
// the host mints `key` once and every retry of the call reuses it.
[[nodiscard]] backend::v1::CancelTradeRequest ToLeaveCancelRequest(common::CharacterId character,
                                                                   common::WorldId world,
                                                                   const ZoneAuthority& authority,
                                                                   const common::RequestKey& key);

// ---- Backend -> zone, client --------------------------------------------------------------------
// How the Backend's outcome reads to the requester: APPLIED, SETTLED and CANCELLED accept;
// STALE_AUTHORITY is kNotOwner, which ends the session as for pickups.
[[nodiscard]] sim::RejectReason ToTradeReject(backend::v1::TradeOutcome outcome);
// The trade as `viewer` sees it; `entity_of` names a character's entity in this zone, or the nil
// id when it is not here.
[[nodiscard]] game::v1::TradeUpdate ToTradeUpdate(
    const backend::v1::TradeState& state, common::CharacterId viewer,
    const std::function<common::EntityId(common::CharacterId)>& entity_of);

}  // namespace sa::zone::codec
