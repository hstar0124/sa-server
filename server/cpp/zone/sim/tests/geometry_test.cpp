#include "sa/common/geometry.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace sa::common {
namespace {

TEST(Vec2, NormalizedHasUnitLengthAndZeroStaysZero) {
    EXPECT_NEAR((Vec2{3.0F, 4.0F}).Normalized().Length(), 1.0F, 1e-6F);
    EXPECT_EQ(Vec2{}.Normalized(), Vec2{});
    EXPECT_TRUE(Vec2{}.IsZero());
}

TEST(Vec2, AVectorWithoutADirectionNormalisesToZero) {
    constexpr float kInf = std::numeric_limits<float>::infinity();
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kHuge = 3e38F;  // finite, but its squared length overflows
    EXPECT_EQ((Vec2{kInf, 0.0F}).Normalized(), Vec2{});
    EXPECT_EQ((Vec2{kNaN, 1.0F}).Normalized(), Vec2{});
    EXPECT_EQ((Vec2{kHuge, kHuge}).Normalized(), Vec2{});
    EXPECT_FALSE((Vec2{kInf, 0.0F}).IsFinite());
    EXPECT_FALSE((Vec2{0.0F, kNaN}).IsFinite());
    EXPECT_TRUE((Vec2{kHuge, 0.0F}).IsFinite());
}

TEST(Vec2, NothingIsWithinDistanceOfANaNPoint) {
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kFar = 1e30F;
    EXPECT_TRUE(WithinDistance({0.0F, 0.0F}, {3.0F, 4.0F}, 5.0F));
    EXPECT_FALSE(WithinDistance({0.0F, 0.0F}, {3.0F, 4.0F}, 4.9F));
    EXPECT_FALSE(WithinDistance({kNaN, 0.0F}, {0.0F, 0.0F}, kFar));
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
