#include "sa/common/geometry.hpp"

#include <gtest/gtest.h>

namespace sa::common {
namespace {

TEST(Vec2, NormalizedHasUnitLengthAndZeroStaysZero) {
    EXPECT_NEAR((Vec2{3.0F, 4.0F}).Normalized().Length(), 1.0F, 1e-6F);
    EXPECT_EQ(Vec2{}.Normalized(), Vec2{});
    EXPECT_TRUE(Vec2{}.IsZero());
}

TEST(Vec2, ArithmeticAndDistance) {
    const Vec2 a{1.0F, 2.0F};
    const Vec2 b{4.0F, 6.0F};
    EXPECT_EQ(a + b, (Vec2{5.0F, 8.0F}));
    EXPECT_EQ(b - a, (Vec2{3.0F, 4.0F}));
    EXPECT_EQ(a * 2.0F, (Vec2{2.0F, 4.0F}));
    EXPECT_FLOAT_EQ(Distance(a, b), 5.0F);
    EXPECT_FLOAT_EQ(DistanceSq(a, b), 25.0F);
}

TEST(Aabb, ContainsIsMinInclusiveMaxExclusive) {
    const Aabb box{.min = {0.0F, 0.0F}, .max = {10.0F, 10.0F}};
    EXPECT_TRUE(box.Contains({0.0F, 0.0F}));
    EXPECT_TRUE(box.Contains({9.99F, 5.0F}));
    EXPECT_FALSE(box.Contains({10.0F, 5.0F}));
    EXPECT_FALSE(box.Contains({-0.01F, 5.0F}));
    EXPECT_TRUE(box.IsValid());
    EXPECT_FALSE((Aabb{.min = {5.0F, 5.0F}, .max = {5.0F, 9.0F}}).IsValid());
}

}  // namespace
}  // namespace sa::common
