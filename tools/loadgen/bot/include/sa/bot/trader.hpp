#pragma once

#include "sa/bot/fighter.hpp"
#include "sa/bot/observation.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace sa::bot {

// Every value comes from the load profile; there are no defaults to drift from it.
struct TraderParams {
    float pair_radius_m = 0.0F;  // notice possible partners this far away
    float open_range_m = 0.0F;   // ask to trade from here; keep it inside the zone's trade.range_m
    std::uint64_t offer_gold = 0;  // put this much on the table, or the whole wallet if less
    // Wall time, compared against the zone's ticks.
    std::chrono::milliseconds request_retry{};  // before asking a partner again
    std::chrono::milliseconds give_up{};        // an open trade older than this is cancelled
    std::chrono::milliseconds cooldown{};       // after a trade closes, before the next one
    FighterParams fight;  // what to do while nobody is near: hunt, which also earns gold
};

// Trades with whoever is near: walks up to the nearest other player, opens a trade, offers a
// little gold, and accepts once both sides have put their offer down. Both parties run this same
// brain, so the handshake needs nothing beyond the TradeUpdates the zone relays. One step at a
// time, as the zone takes them. Deterministic for a seed.
class Trader final : public Brain {
public:
    Trader(std::uint64_t seed, TraderParams params);

    [[nodiscard]] Intent Decide(const Observation& observation) override;

    [[nodiscard]] std::uint32_t opened() const noexcept { return opened_; }
    [[nodiscard]] std::uint32_t settled() const noexcept { return settled_; }

private:
    [[nodiscard]] static const SeenEntity* NearestPlayer(const Observation& observation,
                                                         float within_m);
    [[nodiscard]] Intent InTrade(const Observation& observation, const SeenTrade& trade);
    [[nodiscard]] static common::TickIndex After(const Observation& observation,
                                                 std::chrono::milliseconds interval);

    TraderParams params_;
    Fighter fight_;
    std::optional<common::TradeId> seen_trade_;  // the trade the last decisions were about
    common::TickIndex trade_seen_at_;
    bool offered_ = false;
    std::optional<std::uint32_t> accepted_version_;
    common::TickIndex next_request_at_;
    common::TickIndex idle_until_;
    std::uint32_t opened_ = 0;
    std::uint32_t settled_ = 0;
};

}  // namespace sa::bot
