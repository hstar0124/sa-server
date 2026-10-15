#pragma once

#include "sa/zone/sim/duration.hpp"

#include <cstdint>

namespace sa::zone::sim {

// Converts wall-clock progress into a bounded number of fixed simulation steps.
//
// Design document 7.2: an overloaded zone must not catch up on an unbounded backlog. When the
// accumulated lag exceeds `max_catch_up_ticks` periods, the excess is dropped and counted so
// the operator can see it, instead of freezing the process in a catch-up spiral.
class FixedStepScheduler {
public:
    FixedStepScheduler(TickRate rate, std::uint32_t max_catch_up_ticks);

    // Account for `elapsed` real time; returns how many ticks the caller should run now.
    [[nodiscard]] std::uint32_t Advance(Duration elapsed);

    [[nodiscard]] Duration period() const noexcept { return period_; }
    // Unconsumed time carried into the next Advance(); always < period().
    [[nodiscard]] Duration lag() const noexcept { return accumulated_; }
    // Ticks discarded because the process fell too far behind; monotonic.
    [[nodiscard]] std::uint64_t dropped_ticks() const noexcept { return dropped_ticks_; }

private:
    Duration period_;
    std::uint32_t max_catch_up_ticks_;
    Duration accumulated_{0};
    std::uint64_t dropped_ticks_ = 0;
};

}  // namespace sa::zone::sim
