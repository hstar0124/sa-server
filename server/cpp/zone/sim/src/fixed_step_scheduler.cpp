#include "sa/zone/sim/fixed_step_scheduler.hpp"

#include <algorithm>

namespace sa::zone::sim {

FixedStepScheduler::FixedStepScheduler(TickRate rate, std::uint32_t max_catch_up_ticks)
    : period_(rate.Period()), max_catch_up_ticks_(std::max<std::uint32_t>(1, max_catch_up_ticks)) {}

std::uint32_t FixedStepScheduler::Advance(Duration elapsed) {
    accumulated_ += std::max(elapsed, Duration{0});
    const auto due = static_cast<std::uint64_t>(accumulated_ / period_);
    if (due <= max_catch_up_ticks_) {
        accumulated_ -= period_ * static_cast<Duration::rep>(due);
        return static_cast<std::uint32_t>(due);
    }
    // Too far behind: run the cap, discard the rest of the backlog (but keep the sub-period
    // remainder so the phase of the tick clock is preserved).
    dropped_ticks_ += due - max_catch_up_ticks_;
    accumulated_ %= period_;
    return max_catch_up_ticks_;
}

}  // namespace sa::zone::sim
