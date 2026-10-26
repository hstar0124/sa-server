#include "sa/bot/trader.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sa::bot {
namespace {

constexpr std::uint32_t kServerHz = 10;  // 1000 ms is 10 ticks
const TraderParams kParams{
    .pair_radius_m = 30.0F,
    .open_range_m = 3.0F,
    .offer_gold = 1,
    .request_retry = std::chrono::milliseconds{1000},
    .give_up = std::chrono::milliseconds{10000},
    .cooldown = std::chrono::milliseconds{2000},
    .fight = FighterParams{
        .engage_radius_m = 10.0F,
        .attack_skill = common::SkillId{1},
        .attack_range_m = 2.0F,
        .attack_interval = std::chrono::milliseconds{1000},
        .roam = WandererParams{
            .arrive_distance_m = 1.0F, .retarget_after_ticks = 200, .margin_m = 2.0F}}};
const common::EntityId kPartner{2};

// The trade step an intent carries; a missing one is a test bug, not a case to branch on.
TradeIntent StepOf(const Intent& intent) {
    if (!intent.trade) {
        throw std::logic_error{"expected a trade step"};
    }
    return *intent.trade;
}

std::vector<SeenEntity> PartnerAt(common::Vec2 at) {
    return {SeenEntity{.id = kPartner, .kind = SeenKind::kPlayer, .position = at, .hp = 50}};
}

Observation See(const std::vector<SeenEntity>& visible, std::uint64_t tick,
                std::optional<SeenTrade> trade = std::nullopt, bool pending = false) {
    return Observation{.self_position = {0.0F, 0.0F},
                       .map_size = {200.0F, 200.0F},
                       .self_hp = 100,
                       .self_max_hp = 100,
                       .visible = visible,
                       .portals = {},
                       .zone = common::ZoneId{1},
                       .tick = common::TickIndex{tick},
                       .tick_hz = kServerHz,
                       .gold = 5,
                       .trade = trade,
                       .trade_step_pending = pending};
}

TEST(Trader, WalksUpToTheNearestPlayerAndOpensOnceWithinRange) {
    Trader trader{1, kParams};
    const auto far = PartnerAt({20.0F, 0.0F});
    const Intent walking = trader.Decide(See(far, 1));
    EXPECT_GT(walking.direction.x, 0.9F);
    EXPECT_FALSE(walking.trade.has_value());

    const auto near = PartnerAt({2.0F, 0.0F});
    const Intent opening = trader.Decide(See(near, 2));
    EXPECT_EQ(StepOf(opening).step, TradeStep::kOpen);
    EXPECT_EQ(StepOf(opening).target, kPartner);
    EXPECT_TRUE(opening.direction.IsZero());
    EXPECT_FALSE(trader.Decide(See(near, 3)).trade.has_value()) << "waits before asking again";
    EXPECT_FALSE(trader.Decide(See(near, 12, std::nullopt, true)).trade.has_value())
        << "nothing new while the zone decides a step";
    EXPECT_TRUE(trader.Decide(See(near, 12)).trade.has_value());
}

TEST(Trader, OffersThenAcceptsOnceBothSidesOfferedAndRestsAfterSettlement) {
    Trader trader{1, kParams};
    SeenTrade trade{.id = common::TradeId::Generate(), .version = 0, .other = kPartner};
    const Intent offer = trader.Decide(See({}, 10, trade));
    EXPECT_EQ(StepOf(offer).step, TradeStep::kOffer);
    EXPECT_EQ(StepOf(offer).gold, 1U);

    trade.version = 1;  // only one offer landed so far
    EXPECT_FALSE(trader.Decide(See({}, 11, trade)).trade.has_value());
    trade.version = 2;  // both have offered
    EXPECT_FALSE(trader.Decide(See({}, 12, trade, true)).trade.has_value())
        << "the offer is still being decided";
    const Intent accept = trader.Decide(See({}, 13, trade));
    EXPECT_EQ(StepOf(accept).step, TradeStep::kAccept);
    EXPECT_EQ(StepOf(accept).version, 2U);
    EXPECT_FALSE(trader.Decide(See({}, 14, trade)).trade.has_value()) << "accepted once";

    trade.status = SeenTradeStatus::kCompleted;
    EXPECT_FALSE(trader.Decide(See({}, 15, trade)).trade.has_value());
    EXPECT_EQ(trader.settled(), 1U);
    // Cooldown (2 s = 20 ticks): a partner right next to it is left alone for a while.
    const auto near = PartnerAt({2.0F, 0.0F});
    EXPECT_FALSE(trader.Decide(See(near, 20)).trade.has_value());
    EXPECT_TRUE(trader.Decide(See(near, 40)).trade.has_value());
}

TEST(Trader, GivesUpOnATradeThatNeverCompletes) {
    Trader trader{1, kParams};
    SeenTrade trade{.id = common::TradeId::Generate(), .version = 1, .other = kPartner};
    (void)trader.Decide(See({}, 100, trade));  // offers
    EXPECT_FALSE(trader.Decide(See({}, 150, trade)).trade.has_value()) << "still patient";
    const Intent cancel = trader.Decide(See({}, 200, trade));  // 10 s after it first saw it
    EXPECT_EQ(StepOf(cancel).step, TradeStep::kCancel);
    EXPECT_EQ(StepOf(cancel).trade, trade.id);
}

}  // namespace
}  // namespace sa::bot
