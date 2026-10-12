#include "sa/common/ids.hpp"

#include <gtest/gtest.h>

#include <type_traits>
#include <unordered_set>

namespace sa::common {
namespace {

TEST(StrongId, DefaultIsNone) {
    constexpr CharacterId kNone;
    static_assert(kNone.IsNone());
    EXPECT_EQ(kNone.value(), 0U);
}

TEST(StrongId, DistinctTagsAreDistinctTypes) {
    static_assert(!std::is_same_v<CharacterId, AccountId>);
    static_assert(!std::is_convertible_v<CharacterId, AccountId>);
    static_assert(!std::is_convertible_v<std::uint64_t, CharacterId>);  // explicit only
}

TEST(StrongId, OrdersAndHashesByValue) {
    const OwnerEpoch older{3};
    const OwnerEpoch newer = older.Next();
    EXPECT_LT(older, newer);
    EXPECT_EQ(newer.value(), 4U);

    const std::unordered_set<EntityId> set{EntityId{1}, EntityId{2}, EntityId{1}};
    EXPECT_EQ(set.size(), 2U);
}

}  // namespace
}  // namespace sa::common
