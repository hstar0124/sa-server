#include "trade_codec.hpp"
#include "zone_host_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace sa::zone {
namespace {

using internal::v1::ZoneToGateway;
using namespace host_fixture;  // NOLINT(google-build-using-namespace): test fixtures only

constexpr std::uint64_t kSeller = 42;
constexpr std::uint64_t kBuyer = 43;

internal::v1::SessionAttach AttachAt(const std::string& session, std::uint64_t character,
                                     common::Vec2 at) {
    internal::v1::SessionAttach a = AttachFor(session, character);
    a.mutable_position()->set_x(at.x);
    a.mutable_position()->set_y(at.y);
    return a;
}

internal::v1::SessionInput OpenInput(const std::string& session, std::uint64_t request,
                                     common::EntityId target) {
    internal::v1::SessionInput input;
    input.set_gateway_session_id(SessionWire(session));
    auto* open = input.mutable_message()->mutable_trade_open();
    open->set_request_id(request);
    open->set_target_entity_id(target.value());
    return input;
}

internal::v1::SessionInput AcceptInput(const std::string& session, std::uint64_t request,
                                       const common::TradeId& trade, std::uint32_t version) {
    internal::v1::SessionInput input;
    input.set_gateway_session_id(SessionWire(session));
    auto* accept = input.mutable_message()->mutable_trade_accept();
    accept->set_request_id(request);
    accept->set_trade_id(trade.ToWire());
    accept->set_version(version);
    return input;
}

internal::v1::SessionInput ChangeZoneInput(const std::string& session, std::uint64_t request) {
    internal::v1::SessionInput input;
    input.set_gateway_session_id(SessionWire(session));
    input.mutable_message()->mutable_change_zone()->set_request_id(request);
    input.mutable_message()->mutable_change_zone()->set_portal_id(kGate.value());
    return input;
}

backend::v1::TradeResponse Answer(backend::v1::TradeOutcome outcome, const common::TradeId& trade,
                                  backend::v1::TradeStatus status) {
    backend::v1::TradeResponse response;
    response.set_outcome(outcome);
    backend::v1::TradeState* state = response.mutable_trade();
    state->set_trade_id(trade.ToWire());
    state->set_status(status);
    state->set_version(1);
    state->mutable_initiator()->set_character_id(kSeller);
    state->mutable_initiator()->set_gold(5);
    state->mutable_counterparty()->set_character_id(kBuyer);
    return response;
}

std::vector<game::v1::TradeUpdate> TradeUpdates(const FakeSink& sink, const std::string& session) {
    std::vector<game::v1::TradeUpdate> out;
    for (const auto& m : sink.OfCase(ZoneToGateway::kSessionOutput)) {
        if (m.session_output().gateway_session_id() == SessionWire(session) &&
            m.session_output().message().has_trade_update()) {
            out.push_back(m.session_output().message().trade_update());
        }
    }
    return out;
}

class ZoneHostTrade : public ::testing::Test {
protected:
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    FakeTrades trades;
    ZoneHost host{zone, Identity(), economy, directory, trades, [] { return true; }};
    FakeSink sink;
    common::TradeId trade = common::TradeId::Generate();
    common::EntityId seller;
    common::EntityId buyer;

    // The seller and the buyer stand next to each other at the portal.
    void SetUp() override {
        host.Attach(sink, AttachAt("seller", kSeller, kGatePosition));
        host.Attach(sink, AttachAt("buyer", kBuyer, kGatePosition + common::Vec2{2.0F, 0.0F}));
        for (const auto& m : sink.OfCase(ZoneToGateway::kSessionAttached)) {
            const common::EntityId entity{m.session_attached().entity_id()};
            (m.session_attached().gateway_session_id() == SessionWire("seller") ? seller : buyer) =
                entity;
        }
        sink.sent.clear();
    }

    void Step(int ticks = 1) {
        for (int i = 0; i < ticks; ++i) {
            zone.Tick();
            host.OnTick(zone.current_tick());
        }
    }

    // The seller opened a trade with the buyer and the Backend applied it.
    void Open() {
        host.Input(OpenInput("seller", 7, buyer));
        Step();
        trades.replies.back()(
            Answer(backend::v1::TRADE_OUTCOME_APPLIED, trade, backend::v1::TRADE_STATUS_OPEN));
        Step();
        sink.sent.clear();
    }
};

TEST_F(ZoneHostTrade, AnOpenGoesToTheBackendUnderADigestKeyAndBothPartiesHearTheState) {
    host.Input(OpenInput("seller", 7, buyer));
    Step();

    const auto open = Single(trades.opens, "open");
    EXPECT_EQ(open.character_id(), kSeller);
    EXPECT_EQ(open.counterparty_character_id(), kBuyer);
    EXPECT_EQ(open.authority().owner_epoch(), 1U);
    const sim::TradeRequest as_sent{.character = common::CharacterId{kSeller},
                                    .request_id = common::RequestId{7}};
    EXPECT_EQ(open.request_id(), codec::TradeRequestKey(as_sent, common::OwnerEpoch{1}).ToWire())
        << "a digest of (character, epoch, request), not the client's counter";
    EXPECT_EQ(Single(CommandResults(sink), "pending").status(), game::v1::COMMAND_STATUS_PENDING);
    sink.sent.clear();

    trades.replies.back()(
        Answer(backend::v1::TRADE_OUTCOME_APPLIED, trade, backend::v1::TRADE_STATUS_OPEN));
    Step();
    EXPECT_EQ(Single(CommandResults(sink), "verdict").status(), game::v1::COMMAND_STATUS_ACCEPTED);
    const auto mine = Single(TradeUpdates(sink, "seller"), "seller's update");
    EXPECT_EQ(mine.you().character_id(), kSeller);
    EXPECT_EQ(mine.you().gold(), 5U);
    EXPECT_EQ(mine.other().entity_id(), buyer.value());
    const auto theirs = Single(TradeUpdates(sink, "buyer"), "buyer's update");
    EXPECT_EQ(theirs.you().character_id(), kBuyer);
    EXPECT_EQ(theirs.other().gold(), 5U);
    EXPECT_EQ(theirs.status(), game::v1::TRADE_STATUS_OPEN);
}

TEST_F(ZoneHostTrade, ASettlementReReadsBothInventories) {
    Open();
    const std::size_t reads_before = economy.asset_requests.size();
    host.Input(AcceptInput("buyer", 3, trade, 1));
    Step();
    ASSERT_EQ(trades.accepts.size(), 1U);
    trades.replies.back()(
        Answer(backend::v1::TRADE_OUTCOME_SETTLED, trade, backend::v1::TRADE_STATUS_COMPLETED));

    ASSERT_EQ(economy.asset_requests.size(), reads_before + 2);
    std::vector<std::uint64_t> reread;
    for (std::size_t i = reads_before; i < economy.asset_requests.size(); ++i) {
        reread.push_back(economy.asset_requests[i].character_id());
    }
    std::ranges::sort(reread);
    EXPECT_EQ(reread, (std::vector<std::uint64_t>{kSeller, kBuyer}));
    EXPECT_EQ(host.stats().trades_settled, 1U);
    EXPECT_EQ(TradeUpdates(sink, "seller").back().status(), game::v1::TRADE_STATUS_COMPLETED);
}

TEST_F(ZoneHostTrade, ASessionThatEndsWhileTradingCancelsAndTheCounterpartyHears) {
    Open();
    internal::v1::SessionDetach detach;
    detach.set_gateway_session_id(SessionWire("seller"));
    host.Detach(detach);

    const auto cancel = Single(trades.cancels, "cancel on leave");
    EXPECT_EQ(cancel.character_id(), kSeller);
    EXPECT_TRUE(cancel.trade_id().empty()) << "every open trade of the character";
    EXPECT_EQ(cancel.request_id().size(), common::RequestKey::kBytes);
    trades.replies.back()(
        Answer(backend::v1::TRADE_OUTCOME_CANCELLED, trade, backend::v1::TRADE_STATUS_CANCELLED));
    EXPECT_EQ(TradeUpdates(sink, "buyer").back().status(), game::v1::TRADE_STATUS_CANCELLED);

    // The buyer heard the trade closed, so its own departure asks for nothing.
    detach.set_gateway_session_id(SessionWire("buyer"));
    host.Detach(detach);
    EXPECT_EQ(trades.cancels.size(), 1U);
    EXPECT_EQ(host.stats().trade_cancels_on_leave, 1U);
}

TEST_F(ZoneHostTrade, ADepartureWhileTradingCancelsBeforeOwnershipMoves) {
    Open();
    host.Input(ChangeZoneInput("seller", 9));
    Step();
    EXPECT_TRUE(directory.transfers.empty()) << "the cancel goes first";
    ASSERT_EQ(trades.cancels.size(), 1U);

    trades.replies.back()(
        Answer(backend::v1::TRADE_OUTCOME_CANCELLED, trade, backend::v1::TRADE_STATUS_CANCELLED));
    EXPECT_EQ(Single(directory.transfers, "transfer").character_id(), kSeller);
    EXPECT_EQ(TradeUpdates(sink, "seller").back().status(), game::v1::TRADE_STATUS_CANCELLED)
        << "the leaving player still hears it, before it leaves";
}

TEST_F(ZoneHostTrade, NoAnswerLeavesTheStepRetryableUnderTheSameKey) {
    host.Input(OpenInput("seller", 7, buyer));
    Step();
    trades.replies.back()(std::nullopt);
    Step();
    EXPECT_EQ(CommandResults(sink).back().status(), game::v1::COMMAND_STATUS_UNKNOWN);

    host.Input(OpenInput("seller", 7, buyer));
    Step();
    ASSERT_EQ(trades.opens.size(), 2U);
    EXPECT_EQ(trades.opens[0].request_id(), trades.opens[1].request_id());
}

TEST_F(ZoneHostTrade, AStaleAuthorityEndsTheSessionAsForPickups) {
    host.Input(OpenInput("seller", 7, buyer));
    Step();
    backend::v1::TradeResponse stale;
    stale.set_outcome(backend::v1::TRADE_OUTCOME_STALE_AUTHORITY);
    trades.replies.back()(stale);

    const auto evicted = sink.OfCase(ZoneToGateway::kSessionEvicted);
    ASSERT_EQ(evicted.size(), 1U);
    EXPECT_EQ(evicted.front().session_evicted().gateway_session_id(), SessionWire("seller"));
    EXPECT_EQ(evicted.front().session_evicted().reason(), internal::v1::EVICT_REASON_NOT_OWNER);
}

}  // namespace
}  // namespace sa::zone
