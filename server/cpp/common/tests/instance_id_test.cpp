#include "sa/common/instance_id.hpp"

#include <gtest/gtest.h>

#include <unordered_set>

namespace sa::common {
namespace {

TEST(InstanceId, GeneratedIdsAreDistinctAndRoundTripThroughWire) {
    const InstanceId a = InstanceId::Generate();
    const InstanceId b = InstanceId::Generate();
    EXPECT_NE(a, b);
    EXPECT_FALSE(a.IsNil());
    EXPECT_TRUE(InstanceId{}.IsNil());

    const auto parsed = InstanceId::FromString(a.ToWire());
    EXPECT_EQ(parsed.value_or(InstanceId{}), a);
    EXPECT_EQ(a.ToHex().size(), InstanceId::kBytes * 2);
}

TEST(InstanceId, RejectsWrongLength) {
    EXPECT_FALSE(InstanceId::FromString("short").has_value());
    EXPECT_FALSE(InstanceId::FromString(std::string(17, 'x')).has_value());
}

TEST(InstanceId, HashesIntoUnorderedContainers) {
    std::unordered_set<InstanceId> set;
    const InstanceId a = InstanceId::Generate();
    set.insert(a);
    set.insert(a);
    set.insert(InstanceId::Generate());
    EXPECT_EQ(set.size(), 2U);
    EXPECT_TRUE(set.contains(a));
}

}  // namespace
}  // namespace sa::common
