#include "sa/common/bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace sa::common {
namespace {

TEST(Bytes, WireStringRoundTripKeepsEveryByteIncludingZero) {
    constexpr std::array<std::byte, 4> kBytes{std::byte{'a'}, std::byte{'b'}, std::byte{0},
                                              std::byte{'c'}};
    const std::string wire = ToWireString(kBytes);
    ASSERT_EQ(wire.size(), kBytes.size());
    EXPECT_EQ(wire[2], char{});
    EXPECT_TRUE(std::ranges::equal(AsBytes(wire), kBytes));
}

TEST(Base64, DecodesStandardPaddingVariants) {
    EXPECT_EQ(ToWireString(DecodeBase64("YQ==").value_or(std::vector<std::byte>{})), "a");
    EXPECT_EQ(ToWireString(DecodeBase64("YWI=").value_or(std::vector<std::byte>{})), "ab");
    EXPECT_EQ(ToWireString(DecodeBase64("YWJj").value_or(std::vector<std::byte>{})), "abc");
    EXPECT_EQ(DecodeBase64("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=")
                  .value_or(std::vector<std::byte>{})
                  .size(),
              32U);
}

TEST(Base64, RejectsMalformedInput) {
    EXPECT_FALSE(DecodeBase64("").has_value());
    EXPECT_FALSE(DecodeBase64("abc").has_value()) << "length not a multiple of 4";
    EXPECT_FALSE(DecodeBase64("!!!!").has_value());
}

}  // namespace
}  // namespace sa::common
