#include "tick_runner.hpp"

#include "zone_host_test_support.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace sa::zone {
namespace {

using namespace host_fixture;  // NOLINT(google-build-using-namespace): test fixtures only

constexpr std::uint32_t kCatchUpLimit = 3;
constexpr std::chrono::seconds kStatsInterval{3600};  // never during the test

// The time the runner reads: advanced by the test, not by the wall.
struct FakeClock {
    std::chrono::steady_clock::time_point now{std::chrono::hours{1}};
    TickRunner::Clock AsClock() {
        return [this] { return now; };
    }
};

// A stall far longer than the catch-up limit: the runner runs the limit's worth of ticks, drops
// the rest and counts them, instead of freezing in a catch-up spiral.
TEST(TickRunner, AfterAStallItCatchesUpOnlyToTheLimitAndCountsTheRest) {
    sim::Zone zone = MakeZone();
    asio::io_context io;
    FakeClock clock;
    std::vector<common::TickIndex> ran;
    const sim::TickRate rate = zone.config().simulation_rate;
    TickRunner runner{io,
                      zone,
                      sim::FixedStepScheduler{rate, kCatchUpLimit},
                      kStatsInterval,
                      [&](common::TickIndex tick) {
                          ran.push_back(tick);
                          clock.now += rate.Period() * 100;  // each tick "takes" 100 periods
                      },
                      clock.AsClock()};
    runner.Start();
    clock.now += rate.Period();  // the first wake finds one tick due
    while (ran.size() < 1 + kCatchUpLimit && io.run_one_for(std::chrono::seconds{1}) > 0) {
    }
    runner.Stop();

    EXPECT_EQ(ran.front(), common::TickIndex{1});
    EXPECT_EQ(ran.size(), 1 + kCatchUpLimit) << "the second wake ran only the limit";
    EXPECT_EQ(runner.dropped_ticks(), 100U - kCatchUpLimit);
}

}  // namespace
}  // namespace sa::zone
