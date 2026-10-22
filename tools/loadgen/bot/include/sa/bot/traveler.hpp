#pragma once

#include "sa/bot/observation.hpp"
#include "sa/bot/wanderer.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace sa::bot {

// Every value comes from the load profile; there are no defaults to drift from it.
struct TravelerParams {
    // Roam this long after arriving before heading for a portal. Wall time, compared against the
    // zone's ticks.
    std::chrono::milliseconds dwell{};
    // Ask again after this long when the zone has not changed. The network client repeats the
    // same request until the zone decides it, so asking again is safe.
    std::chrono::milliseconds request_retry{};
    // Walk this close to a portal's centre before asking; positions on the client lag the zone,
    // so asking at the rim would be refused as out of range.
    float portal_approach_m = 0.0F;
    WandererParams roam;
};

// Crosses zones: after dwelling a while it walks to the nearest portal, asks to leave, and waits
// for the next zone (a changed zone id) before dwelling there. Deterministic for a seed; the
// roaming between crossings is a Wanderer.
class Traveler final : public Brain {
public:
    Traveler(std::uint64_t seed, TravelerParams params);

    [[nodiscard]] Intent Decide(const Observation& observation) override;

    [[nodiscard]] std::uint32_t crossings() const noexcept { return crossings_; }
    [[nodiscard]] bool waiting() const noexcept { return asked_in_.has_value(); }

private:
    [[nodiscard]] static const SeenPortal* NearestPortal(const Observation& observation);
    [[nodiscard]] Intent Ask(const Observation& observation, const SeenPortal& portal);

    TravelerParams params_;
    Wanderer roam_;
    std::optional<common::ZoneId> zone_;
    common::TickIndex leave_after_;
    std::optional<common::ZoneId> asked_in_;  // asked to leave this zone; waiting for another
    common::TickIndex ask_again_at_;
    std::uint32_t crossings_ = 0;
};

}  // namespace sa::bot
