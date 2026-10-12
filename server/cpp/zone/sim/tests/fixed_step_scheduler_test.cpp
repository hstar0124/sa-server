#include "sa/zone/sim/fixed_step_scheduler.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace sa::zone::sim {
namespace {

using namespace std::chrono_literals;

constexpr TickRate kTwentyHz{.hz = 20};  // 50 ms period
constexpr std::uint32_t kMaxCatchUp = 4;

TEST(TickRate, PeriodIsOneSecondOverHz) {
    EXPECT_EQ(kTwentyHz.Period(), 50ms);
    EXPECT_EQ(TickRate{.hz = 60}.Period(), std::chrono::nanoseconds{1'000'000'000 / 60});
}

TEST(FixedStepScheduler, AccumulatesSubPeriodTimeUntilATickIsDue) {
    FixedStepScheduler scheduler{kTwentyHz, kMaxCatchUp};
    EXPECT_EQ(scheduler.Advance(30ms), 0U);
    EXPECT_EQ(scheduler.lag(), 30ms);
    EXPECT_EQ(scheduler.Advance(30ms), 1U);
    EXPECT_EQ(scheduler.lag(), 10ms);
}

TEST(FixedStepScheduler, RunsSeveralTicksAfterAModerateStall) {
    FixedStepScheduler scheduler{kTwentyHz, kMaxCatchUp};
    EXPECT_EQ(scheduler.Advance(150ms), 3U);
    EXPECT_EQ(scheduler.dropped_ticks(), 0U);
    EXPECT_EQ(scheduler.lag(), 0ms);
}

TEST(FixedStepScheduler, CapsCatchUpAndCountsDroppedTicks) {
    FixedStepScheduler scheduler{kTwentyHz, kMaxCatchUp};
    // 1.02 s stall = 20 ticks due + 20 ms remainder.
    EXPECT_EQ(scheduler.Advance(1020ms), kMaxCatchUp);
    EXPECT_EQ(scheduler.dropped_ticks(), 20U - kMaxCatchUp);
    EXPECT_EQ(scheduler.lag(), 20ms) << "phase remainder is preserved";
    EXPECT_EQ(scheduler.Advance(30ms), 1U) << "normal cadence resumes";
}

TEST(FixedStepScheduler, NegativeElapsedIsIgnored) {
    FixedStepScheduler scheduler{kTwentyHz, kMaxCatchUp};
    EXPECT_EQ(scheduler.Advance(-10ms), 0U);
    EXPECT_EQ(scheduler.lag(), 0ms);
}

TEST(FixedStepScheduler, ZeroCatchUpIsClampedToOne) {
    FixedStepScheduler scheduler{kTwentyHz, 0};
    EXPECT_EQ(scheduler.Advance(500ms), 1U);
}

}  // namespace
}  // namespace sa::zone::sim
