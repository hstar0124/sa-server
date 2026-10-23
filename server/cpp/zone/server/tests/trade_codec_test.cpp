#include "trade_codec.hpp"

#include <gtest/gtest.h>

#include <string>

namespace sa::zone::codec {
namespace {

TEST(TradeCodec, AMalformedTradeOrItemIdNeverReachesTheSimulation) {
    game::v1::TradeAccept accept;
    accept.set_request_id(1);
    accept.set_trade_id("short");
    EXPECT_EQ(ToTradeCommand(common::EntityId{1}, accept).error(), InputError::kMalformedId);

    game::v1::TradeOffer offer;
    offer.set_request_id(2);
    offer.set_trade_id(common::TradeId::Generate().ToWire());
    offer.add_item_instance_ids(common::ItemInstanceId::Generate().ToWire());
    offer.add_item_instance_ids(std::string(3, 'x'));
    EXPECT_EQ(ToTradeCommand(common::EntityId{1}, offer).error(), InputError::kMalformedId);

    offer.mutable_item_instance_ids()->RemoveLast();
    offer.set_gold(9);
    const auto command = ToTradeCommand(common::EntityId{1}, offer);
    ASSERT_TRUE(command.has_value());
    EXPECT_EQ(command->action, sim::TradeAction::kOffer);
    EXPECT_EQ(command->items.size(), 1U);
    EXPECT_EQ(command->gold, 9U);
}

TEST(TradeCodec, OutcomesReadAsTheSameVerdictsAsOtherBackendAnswers) {
    EXPECT_EQ(ToTradeReject(backend::v1::TRADE_OUTCOME_SETTLED), sim::RejectReason::kNone);
    EXPECT_EQ(ToTradeReject(backend::v1::TRADE_OUTCOME_CANCELLED), sim::RejectReason::kNone);
    EXPECT_EQ(ToTradeReject(backend::v1::TRADE_OUTCOME_STALE_AUTHORITY),
              sim::RejectReason::kNotOwner)
        << "ends the session, as a stale pickup does";
    EXPECT_EQ(ToTradeReject(backend::v1::TRADE_OUTCOME_REQUEST_MISMATCH),
              sim::RejectReason::kInvalidRequest);
    EXPECT_EQ(ToTradeReject(backend::v1::TRADE_OUTCOME_INVENTORY_FULL),
              sim::RejectReason::kInventoryFull);
    EXPECT_EQ(ToTradeReject(backend::v1::TRADE_OUTCOME_UNSPECIFIED),
              sim::RejectReason::kTradeClosed);
}

TEST(TradeCodec, ATradeUpdateIsWrittenFromTheViewersSide) {
    backend::v1::TradeState state;
    state.set_trade_id(common::TradeId::Generate().ToWire());
    state.set_status(backend::v1::TRADE_STATUS_OPEN);
    state.set_version(4);
    state.mutable_initiator()->set_character_id(1);
    state.mutable_initiator()->set_gold(10);
    state.mutable_counterparty()->set_character_id(2);
    state.mutable_counterparty()->set_accepted(true);
    const auto entity_of = [](common::CharacterId character) {
        return character == common::CharacterId{2} ? common::EntityId{77} : common::EntityId{};
    };

    const game::v1::TradeUpdate seen_by_2 = ToTradeUpdate(state, common::CharacterId{2}, entity_of);
    EXPECT_EQ(seen_by_2.version(), 4U);
    EXPECT_EQ(seen_by_2.you().character_id(), 2U);
    EXPECT_EQ(seen_by_2.you().entity_id(), 77U);
    EXPECT_TRUE(seen_by_2.you().accepted());
    EXPECT_EQ(seen_by_2.other().gold(), 10U);
    EXPECT_EQ(seen_by_2.other().entity_id(), 0U) << "not in this zone";
}

TEST(TradeCodec, TheRequestKeyNamesOneStepOfOneCharacterInOneGeneration) {
    const sim::TradeRequest step{.character = common::CharacterId{5},
                                 .request_id = common::RequestId{9}};
    const auto key = TradeRequestKey(step, common::OwnerEpoch{1});
    EXPECT_EQ(key, TradeRequestKey(step, common::OwnerEpoch{1})) << "a retry finds its answer";
    EXPECT_NE(key, TradeRequestKey(step, common::OwnerEpoch{2})) << "a new session starts afresh";
    sim::TradeRequest other = step;
    other.character = common::CharacterId{6};
    EXPECT_NE(key, TradeRequestKey(other, common::OwnerEpoch{1}));
}

}  // namespace
}  // namespace sa::zone::codec
