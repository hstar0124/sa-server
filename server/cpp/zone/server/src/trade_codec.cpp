#include "trade_codec.hpp"

#include <span>
#include <string>
#include <string_view>

namespace sa::zone::codec {
namespace {

std::expected<common::TradeId, InputError> TradeIdOf(const std::string& wire) {
    const auto id = common::TradeId::FromString(wire);
    if (!id) {
        return std::unexpected(InputError::kMalformedId);
    }
    return *id;
}

template <typename Request>
void FillHeader(Request& out, common::CharacterId character, common::WorldId world,
                const ZoneAuthority& authority, const common::RequestKey& key) {
    out.set_request_id(key.ToWire());
    out.set_world_id(world.value());
    out.set_character_id(character.value());
    Fill(*out.mutable_authority(), authority);
}

template <typename Request>
void FillHeader(Request& out, const sim::TradeRequest& request, common::WorldId world,
                const ZoneAuthority& authority) {
    FillHeader(out, request.character, world, authority, TradeRequestKey(request, authority.epoch));
}

void FillParty(game::v1::TradeParty& out, const backend::v1::TradeSide& side,
               common::EntityId entity) {
    out.set_character_id(side.character_id());
    out.set_entity_id(entity.value());
    out.set_gold(side.gold());
    out.set_accepted(side.accepted());
    for (const backend::v1::TradeItem& item : side.items()) {
        game::v1::TradeOfferedItem* offered = out.add_items();
        offered->set_item_instance_id(item.item_instance_id());
        offered->set_item_template_id(item.item_template_id());
    }
}

game::v1::TradeStatus ToProto(backend::v1::TradeStatus status) {
    switch (status) {
        case backend::v1::TRADE_STATUS_OPEN:
            return game::v1::TRADE_STATUS_OPEN;
        case backend::v1::TRADE_STATUS_COMPLETED:
            return game::v1::TRADE_STATUS_COMPLETED;
        case backend::v1::TRADE_STATUS_CANCELLED:
            return game::v1::TRADE_STATUS_CANCELLED;
        case backend::v1::TRADE_STATUS_UNSPECIFIED:
        case backend::v1::TradeStatus_INT_MIN_SENTINEL_DO_NOT_USE_:
        case backend::v1::TradeStatus_INT_MAX_SENTINEL_DO_NOT_USE_:
            break;
    }
    return game::v1::TRADE_STATUS_UNSPECIFIED;
}

}  // namespace

// ---- client -> zone -----------------------------------------------------------------------------

std::expected<sim::TradeCommand, InputError> ToTradeCommand(common::EntityId player,
                                                            const game::v1::TradeOpen& open) {
    return sim::TradeCommand{.player = player,
                             .request_id = common::RequestId{open.request_id()},
                             .action = sim::TradeAction::kOpen,
                             .target = common::EntityId{open.target_entity_id()}};
}

std::expected<sim::TradeCommand, InputError> ToTradeCommand(common::EntityId player,
                                                            const game::v1::TradeOffer& offer) {
    const auto trade = TradeIdOf(offer.trade_id());
    if (!trade) {
        return std::unexpected(trade.error());
    }
    sim::TradeCommand command{.player = player,
                              .request_id = common::RequestId{offer.request_id()},
                              .action = sim::TradeAction::kOffer,
                              .trade = *trade,
                              .gold = offer.gold()};
    command.items.reserve(static_cast<std::size_t>(offer.item_instance_ids_size()));
    for (const std::string& wire : offer.item_instance_ids()) {
        const auto item = common::ItemInstanceId::FromString(wire);
        if (!item) {
            return std::unexpected(InputError::kMalformedId);
        }
        command.items.push_back(*item);
    }
    return command;
}

std::expected<sim::TradeCommand, InputError> ToTradeCommand(common::EntityId player,
                                                            const game::v1::TradeAccept& accept) {
    const auto trade = TradeIdOf(accept.trade_id());
    if (!trade) {
        return std::unexpected(trade.error());
    }
    return sim::TradeCommand{.player = player,
                             .request_id = common::RequestId{accept.request_id()},
                             .action = sim::TradeAction::kAccept,
                             .trade = *trade,
                             .version = accept.version()};
}

std::expected<sim::TradeCommand, InputError> ToTradeCommand(common::EntityId player,
                                                            const game::v1::TradeCancel& cancel) {
    const auto trade = TradeIdOf(cancel.trade_id());
    if (!trade) {
        return std::unexpected(trade.error());
    }
    return sim::TradeCommand{.player = player,
                             .request_id = common::RequestId{cancel.request_id()},
                             .action = sim::TradeAction::kCancel,
                             .trade = *trade};
}

// ---- zone -> Backend ----------------------------------------------------------------------------

common::RequestKey TradeRequestKey(const sim::TradeRequest& request, common::OwnerEpoch epoch) {
    static constexpr std::string_view kDomain = "trade-request";
    const common::HmacSha256Digest digest =
        CommandDigest(kDomain, request.character, epoch, request.request_id);
    return common::RequestKey::FromExact(std::span{digest}.first<common::RequestKey::kBytes>());
}

backend::v1::OpenTradeRequest ToOpenTradeRequest(const sim::TradeRequest& request,
                                                 common::WorldId world,
                                                 const ZoneAuthority& authority) {
    backend::v1::OpenTradeRequest out;
    FillHeader(out, request, world, authority);
    out.set_counterparty_character_id(request.counterparty.value());
    return out;
}

backend::v1::OfferTradeRequest ToOfferTradeRequest(const sim::TradeRequest& request,
                                                   common::WorldId world,
                                                   const ZoneAuthority& authority) {
    backend::v1::OfferTradeRequest out;
    FillHeader(out, request, world, authority);
    out.set_trade_id(request.trade.ToWire());
    for (const common::ItemInstanceId& item : request.items) {
        out.add_item_instance_ids(item.ToWire());
    }
    out.set_gold(request.gold);
    return out;
}

backend::v1::AcceptTradeRequest ToAcceptTradeRequest(const sim::TradeRequest& request,
                                                     common::WorldId world,
                                                     const ZoneAuthority& authority) {
    backend::v1::AcceptTradeRequest out;
    FillHeader(out, request, world, authority);
    out.set_trade_id(request.trade.ToWire());
    out.set_version(request.version);
    return out;
}

backend::v1::CancelTradeRequest ToCancelTradeRequest(const sim::TradeRequest& request,
                                                     common::WorldId world,
                                                     const ZoneAuthority& authority) {
    backend::v1::CancelTradeRequest out;
    FillHeader(out, request, world, authority);
    out.set_trade_id(request.trade.ToWire());
    return out;
}

backend::v1::CancelTradeRequest ToLeaveCancelRequest(common::CharacterId character,
                                                     common::WorldId world,
                                                     const ZoneAuthority& authority,
                                                     const common::RequestKey& key) {
    backend::v1::CancelTradeRequest out;  // no trade id: every open trade of the character
    FillHeader(out, character, world, authority, key);
    return out;
}

// ---- Backend -> zone, client --------------------------------------------------------------------

sim::RejectReason ToTradeReject(backend::v1::TradeOutcome outcome) {
    switch (outcome) {
        case backend::v1::TRADE_OUTCOME_APPLIED:
        case backend::v1::TRADE_OUTCOME_SETTLED:
        case backend::v1::TRADE_OUTCOME_CANCELLED:
            return sim::RejectReason::kNone;
        case backend::v1::TRADE_OUTCOME_STALE_AUTHORITY:
            return sim::RejectReason::kNotOwner;
        case backend::v1::TRADE_OUTCOME_COUNTERPARTY_NOT_HERE:
            return sim::RejectReason::kTradeCounterpartyLeft;
        case backend::v1::TRADE_OUTCOME_SELF_TRADE:
            return sim::RejectReason::kInvalidTarget;
        case backend::v1::TRADE_OUTCOME_ALREADY_TRADING:
            return sim::RejectReason::kTradeBusy;
        case backend::v1::TRADE_OUTCOME_VERSION_MISMATCH:
            return sim::RejectReason::kTradeVersionMismatch;
        case backend::v1::TRADE_OUTCOME_ITEM_UNAVAILABLE:
            return sim::RejectReason::kTradeItemUnavailable;
        case backend::v1::TRADE_OUTCOME_INSUFFICIENT_GOLD:
            return sim::RejectReason::kNotEnoughGold;
        case backend::v1::TRADE_OUTCOME_INVENTORY_FULL:
            return sim::RejectReason::kInventoryFull;
        case backend::v1::TRADE_OUTCOME_REQUEST_MISMATCH:
            return sim::RejectReason::kInvalidRequest;
        case backend::v1::TRADE_OUTCOME_NOT_PARTICIPANT:
        case backend::v1::TRADE_OUTCOME_TRADE_CLOSED:
        case backend::v1::TRADE_OUTCOME_UNSPECIFIED:
        case backend::v1::TradeOutcome_INT_MIN_SENTINEL_DO_NOT_USE_:
        case backend::v1::TradeOutcome_INT_MAX_SENTINEL_DO_NOT_USE_:
            break;
    }
    return sim::RejectReason::kTradeClosed;
}

game::v1::TradeUpdate ToTradeUpdate(
    const backend::v1::TradeState& state, common::CharacterId viewer,
    const std::function<common::EntityId(common::CharacterId)>& entity_of) {
    game::v1::TradeUpdate out;
    out.set_trade_id(state.trade_id());
    out.set_status(ToProto(state.status()));
    out.set_version(state.version());
    const bool viewer_initiated = state.initiator().character_id() == viewer.value();
    const backend::v1::TradeSide& mine =
        viewer_initiated ? state.initiator() : state.counterparty();
    const backend::v1::TradeSide& theirs =
        viewer_initiated ? state.counterparty() : state.initiator();
    FillParty(*out.mutable_you(), mine, entity_of(common::CharacterId{mine.character_id()}));
    FillParty(*out.mutable_other(), theirs, entity_of(common::CharacterId{theirs.character_id()}));
    return out;
}

}  // namespace sa::zone::codec
