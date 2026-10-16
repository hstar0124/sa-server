#include "sa/zone/sim/rng.hpp"

#include <gtest/gtest.h>

namespace sa::zone::sim {
namespace {

TEST(Rng, SameSeedSameSequence) {
    Rng a{42};
    Rng b{42};
    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(a.NextU64(), b.NextU64());
    }
}

TEST(Rng, RangeIsInclusiveAndUnitIsHalfOpen) {
    Rng rng{7};
    for (int i = 0; i < 10'000; ++i) {
        const int v = rng.Range(3, 5);
        ASSERT_GE(v, 3);
        ASSERT_LE(v, 5);
        const float u = rng.NextUnit();
        ASSERT_GE(u, 0.0F);
        ASSERT_LT(u, 1.0F);
    }
}

}  // namespace
}  // namespace sa::zone::sim
