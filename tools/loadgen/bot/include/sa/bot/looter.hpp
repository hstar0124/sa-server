#pragma once

#include "sa/bot/fighter.hpp"
#include "sa/bot/observation.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace sa::bot {

// Every value comes from the load profile; there are no defaults to drift from it.
struct LooterParams {
    float loot_radius_m = 0.0F;   // notice ground items this far away
    float pickup_range_m = 0.0F;  // ask for the item from here; must stay below the zone's rule
    // Before asking again for an item that still lies there. Wall time, compared against the
    // server's ticks like the fighter's attack interval.
    std::chrono::milliseconds pickup_retry{};
    FighterParams fight;  // what to do when nothing lies around
};

// Collects what the fighting leaves behind: walks to the nearest ground item it can see and asks
// the zone for it, otherwise hunts like a Fighter. Deterministic for a seed.
class Looter final : public Brain {
public:
    Looter(std::uint64_t seed, LooterParams params);

    [[nodiscard]] Intent Decide(const Observation& observation) override;

    [[nodiscard]] std::optional<common::EntityId> target() const noexcept { return target_; }
    [[nodiscard]] std::uint32_t pickups() const noexcept { return pickups_; }

private:
    [[nodiscard]] static const SeenEntity* NearestItem(const Observation& observation,
                                                       float within_m);

    LooterParams params_;
    Fighter fight_;
    std::optional<common::EntityId> target_;
    common::TickIndex next_pickup_tick_;
    std::uint32_t pickups_ = 0;
};

}  // namespace sa::bot
