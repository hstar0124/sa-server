#include "sa/common/lease.hpp"

#include <gtest/gtest.h>

namespace sa::common {
namespace {

using namespace std::chrono_literals;

TEST(LeaseState, NotAliveUntilFirstRenewal) {
    const LeaseState lease{1s};
    EXPECT_FALSE(lease.IsAlive(LeaseState::Clock::time_point{}));
}

TEST(LeaseState, AliveUntilDurationMinusMargin) {
    LeaseState lease{2s};
    const auto t0 = LeaseState::Clock::time_point{} + 100s;
    lease.Renewed(t0, 15s);
    EXPECT_TRUE(lease.IsAlive(t0 + 12s));
    EXPECT_FALSE(lease.IsAlive(t0 + 13s)) << "margin is subtracted";
    lease.Renewed(t0 + 13s, 15s);
    EXPECT_TRUE(lease.IsAlive(t0 + 20s));
}

}  // namespace
}  // namespace sa::common
