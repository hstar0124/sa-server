#include "sa/zone/sim/zone.hpp"

#include "test_maps.hpp"

#include <gtest/gtest.h>

#include <variant>
#include <vector>

namespace sa::zone::sim {
namespace {

using common::EntityId;
using common::RequestId;
using common::Vec2;

// The command results delivered to `player` since the last drain, in order.
std::vector<CommandResultEvent> ResultsFor(Zone& zone, EntityId player) {
    std::vector<CommandResultEvent> out;
    for (const DeliveredEvent& delivered : zone.DrainEvents()) {
        if (delivered.recipient == player) {
            if (const auto* result = std::get_if<CommandResultEvent>(&delivered.event)) {
                out.push_back(*result);
            }
        }
    }
    return out;
}

std::vector<TradeRequest> TradeRequests(Zone& zone) {
    std::vector<TradeRequest> out;
    for (HostRequest& request : zone.DrainHostRequests()) {
        if (auto* trade = std::get_if<TradeRequest>(&request)) {
            out.push_back(*trade);
        }
    }
    return out;
}

TradeCommand Offer(EntityId player, std::uint64_t request, common::TradeId trade) {
    return TradeCommand{.player = player,
                        .request_id = RequestId{request},
                        .action = TradeAction::kOffer,
                        .trade = trade,
                        .items = {common::ItemInstanceId::Generate()},
                        .gold = 5};
}

TEST(ZoneTrade, OpeningRequiresALiveCounterpartyWithinTradeRange) {
    Zone zone = test::MakeZone();  // trade range 5 m
    const EntityId a = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const EntityId near = test::Spawn(zone, 2, Vec2{53.0F, 50.0F});
    const EntityId far = test::Spawn(zone, 3, Vec2{60.0F, 50.0F});

    zone.Submit(TradeCommand{.player = a, .request_id = RequestId{1}, .target = far});
    zone.Submit(TradeCommand{.player = a, .request_id = RequestId{2}, .target = a});
    zone.Submit(TradeCommand{.player = a, .request_id = RequestId{3}, .target = EntityId{999}});
    zone.Submit(TradeCommand{.player = a, .request_id = RequestId{}, .target = near});
    zone.Submit(TradeCommand{.player = a, .request_id = RequestId{4}, .target = near});
    zone.Tick();

    const auto results = ResultsFor(zone, a);
    ASSERT_EQ(results.size(), 5U);
    EXPECT_EQ(results[0].reason, RejectReason::kOutOfRange);
    EXPECT_EQ(results[1].reason, RejectReason::kInvalidTarget);
    EXPECT_EQ(results[2].reason, RejectReason::kInvalidTarget);
    EXPECT_EQ(results[3].reason, RejectReason::kInvalidRequest);
    EXPECT_EQ(results[4].status, CommandStatus::kPending);

    const auto requests = TradeRequests(zone);
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_EQ(requests[0].action, TradeAction::kOpen);
    EXPECT_EQ(requests[0].character, common::CharacterId{1});
    EXPECT_EQ(requests[0].counterparty, common::CharacterId{2});
    EXPECT_EQ(zone.stats().trade_steps_requested, 1U);
}

TEST(ZoneTrade, StepsAfterOpeningGoToTheBackendAndAFinalVerdictIsRemembered) {
    Zone zone = test::MakeZone();
    const EntityId a = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const common::TradeId trade = common::TradeId::Generate();
    zone.Submit(Offer(a, 10, trade));
    zone.Tick();
    EXPECT_EQ(ResultsFor(zone, a).front().status, CommandStatus::kPending);
    const auto requests = TradeRequests(zone);
    ASSERT_EQ(requests.size(), 1U);
    EXPECT_EQ(requests[0].action, TradeAction::kOffer);
    EXPECT_EQ(requests[0].gold, 5U);
    EXPECT_EQ(requests[0].trade, trade);
    EXPECT_EQ(requests[0].items.size(), 1U);

    // The Backend refuses: the verdict is delivered and a retry of the same id replays it.
    zone.ResolveTrade(requests[0], RejectReason::kNotEnoughGold);
    auto results = ResultsFor(zone, a);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].status, CommandStatus::kRejected);
    EXPECT_EQ(results[0].reason, RejectReason::kNotEnoughGold);
    zone.Submit(Offer(a, 10, trade));
    zone.Tick();
    results = ResultsFor(zone, a);
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].reason, RejectReason::kNotEnoughGold);
    EXPECT_TRUE(TradeRequests(zone).empty()) << "a remembered verdict is not sent again";
}

TEST(ZoneTrade, AStepTheBackendIsDecidingIsSentOnceAndTheNextWaitsForIt) {
    Zone zone = test::MakeZone();
    const EntityId a = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const common::TradeId trade = common::TradeId::Generate();
    zone.Submit(Offer(a, 20, trade));
    zone.Tick();
    const auto requests = TradeRequests(zone);
    ASSERT_EQ(requests.size(), 1U);
    (void)zone.DrainEvents();

    zone.Submit(Offer(a, 20, trade));  // the client did not hear back yet and asks again
    zone.Submit(TradeCommand{.player = a,
                             .request_id = RequestId{21},
                             .action = TradeAction::kAccept,
                             .trade = trade,
                             .version = 1});
    zone.Tick();
    auto results = ResultsFor(zone, a);
    ASSERT_EQ(results.size(), 2U);
    EXPECT_EQ(results[0].status, CommandStatus::kPending) << "the same step, still deciding";
    EXPECT_EQ(results[1].status, CommandStatus::kUnknown) << "a second step: send it again later";
    EXPECT_TRUE(TradeRequests(zone).empty());

    // No answer at all: nothing is remembered, so the retry reaches the Backend again.
    zone.ResolveTrade(requests[0], RejectReason::kServiceUnavailable);
    EXPECT_EQ(ResultsFor(zone, a).front().status, CommandStatus::kUnknown);
    zone.Submit(Offer(a, 20, trade));
    zone.Tick();
    EXPECT_EQ(TradeRequests(zone).size(), 1U);
}

TEST(ZoneTrade, ALeavingPlayerNeitherTradesNorIsTradedWith) {
    // A portal right under the leaver; its ChangeZone stays undecided, so it is departing.
    MapData map = test::OpenField();
    map.portals.push_back(Portal{.id = PortalId{1},
                                 .position = Vec2{52.0F, 50.0F},
                                 .radius_m = 3.0F,
                                 .to_zone = common::ZoneId{2},
                                 .arrival = Vec2{5.0F, 5.0F}});
    Zone zone = test::MakeZone(map);
    const EntityId stayer = test::Spawn(zone, 1, Vec2{50.0F, 50.0F});
    const EntityId leaver = test::Spawn(zone, 2, Vec2{52.0F, 50.0F});
    zone.Submit(
        ChangeZoneCommand{.player = leaver, .request_id = RequestId{1}, .portal = PortalId{1}});
    zone.Tick();
    ASSERT_TRUE(zone.IsDeparting(leaver));
    (void)zone.DrainEvents();

    zone.Submit(Offer(leaver, 2, common::TradeId::Generate()));
    zone.Submit(TradeCommand{.player = stayer, .request_id = RequestId{3}, .target = leaver});
    zone.Tick();
    std::vector<RejectReason> leaver_reasons;
    std::vector<RejectReason> stayer_reasons;
    for (const DeliveredEvent& delivered : zone.DrainEvents()) {
        if (const auto* result = std::get_if<CommandResultEvent>(&delivered.event)) {
            (delivered.recipient == leaver ? leaver_reasons : stayer_reasons)
                .push_back(result->reason);
        }
    }
    EXPECT_EQ(leaver_reasons, std::vector{RejectReason::kTransferInProgress});
    EXPECT_EQ(stayer_reasons, std::vector{RejectReason::kInvalidTarget})
        << "a leaving counterparty is about to belong to another zone";
    EXPECT_TRUE(TradeRequests(zone).empty());
}

}  // namespace
}  // namespace sa::zone::sim
