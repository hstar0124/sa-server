#include "sa/bot/trader.hpp"

#include <algorithm>

namespace sa::bot {
namespace {

// The Backend opens a trade at version 0 and every offer adds one: once both sides have offered,
// the version is at least this.
constexpr std::uint32_t kBothOffered = 2;

}  // namespace

Trader::Trader(std::uint64_t seed, TraderParams params)
    : params_(params), fight_(seed, params.fight) {}

const SeenEntity* Trader::NearestPlayer(const Observation& observation, float within_m) {
    const SeenEntity* best = nullptr;
    float best_sq = within_m * within_m;
    for (const SeenEntity& seen : observation.visible) {
        if (seen.kind != SeenKind::kPlayer || seen.hp <= 0) {
            continue;
        }
        const float d_sq = common::DistanceSq(observation.self_position, seen.position);
        if (d_sq < best_sq || (best != nullptr && d_sq == best_sq && seen.id < best->id)) {
            best = &seen;
            best_sq = d_sq;
        }
    }
    return best;
}

common::TickIndex Trader::After(const Observation& observation,
                                std::chrono::milliseconds interval) {
    return common::TickIndex{
        observation.tick.value() +
        std::max<std::uint64_t>(1, TicksCovering(interval, observation.tick_hz))};
}

Intent Trader::Decide(const Observation& observation) {
    if (observation.tick_hz == 0) {
        return Intent{};  // not welcomed yet: no zone, no clock
    }
    if (observation.trade) {
        return InTrade(observation, *observation.trade);
    }
    seen_trade_.reset();
    if (observation.self_hp <= 0) {
        return Intent{};
    }
    const SeenEntity* partner = observation.tick < idle_until_
                                    ? nullptr
                                    : NearestPlayer(observation, params_.pair_radius_m);
    if (partner == nullptr) {
        return fight_.Decide(observation);
    }
    if (!common::WithinDistance(observation.self_position, partner->position,
                                params_.open_range_m)) {
        return Intent{.direction = (partner->position - observation.self_position).Normalized()};
    }
    Intent intent;  // stand still next to the partner
    if (!observation.trade_step_pending && observation.tick >= next_request_at_) {
        // Both may ask at once; the Backend seats one trade and the other side hears of it.
        intent.trade = TradeIntent{.step = TradeStep::kOpen, .target = partner->id};
        next_request_at_ = After(observation, params_.request_retry);
        ++opened_;
    }
    return intent;
}

Intent Trader::InTrade(const Observation& observation, const SeenTrade& trade) {
    if (seen_trade_ != trade.id) {
        seen_trade_ = trade.id;
        trade_seen_at_ = observation.tick;
        offered_ = false;
        accepted_version_.reset();
    }
    Intent intent;  // trading parties stand still
    if (trade.status != SeenTradeStatus::kOpen) {
        if (trade.status == SeenTradeStatus::kCompleted) {
            ++settled_;
        }
        idle_until_ = After(observation, params_.cooldown);
        return intent;
    }
    if (observation.trade_step_pending) {
        return intent;  // the zone takes one step at a time
    }
    if (observation.tick >=
        common::TickIndex{trade_seen_at_.value() +
                          TicksCovering(params_.give_up, observation.tick_hz)}) {
        if (observation.tick >= next_request_at_) {
            intent.trade = TradeIntent{.step = TradeStep::kCancel, .trade = trade.id};
            next_request_at_ = After(observation, params_.request_retry);
        }
        return intent;
    }
    if (!offered_) {
        intent.trade = TradeIntent{.step = TradeStep::kOffer,
                                   .trade = trade.id,
                                   .gold = std::min(params_.offer_gold, observation.gold)};
        offered_ = true;
        return intent;
    }
    if (trade.version >= kBothOffered && !trade.i_accepted && accepted_version_ != trade.version) {
        intent.trade =
            TradeIntent{.step = TradeStep::kAccept, .trade = trade.id, .version = trade.version};
        accepted_version_ = trade.version;
    }
    return intent;
}

}  // namespace sa::bot
