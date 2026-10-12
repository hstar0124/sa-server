#include "sa/common/hmac.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace sa::common {
namespace {

std::span<const std::byte> Bytes(std::string_view s) {
    return std::as_bytes(std::span{s});
}

// RFC 4231 test case 2: key "Jefe", data "what do ya want for nothing?".
TEST(HmacSha256, MatchesRfc4231Vector) {
    const auto digest = HmacSha256(Bytes("Jefe"), Bytes("what do ya want for nothing?"));
    constexpr std::array<std::uint8_t, 32> kExpected{
        0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e, 0x6a, 0x04, 0x24,
        0x26, 0x08, 0x95, 0x75, 0xc7, 0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27,
        0x39, 0x83, 0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43};
    EXPECT_TRUE(std::ranges::equal(digest, kExpected, [](std::byte b, std::uint8_t e) {
        return std::to_integer<std::uint8_t>(b) == e;
    }));
}

TEST(ConstantTimeEquals, ComparesContentAndLength) {
    const auto a = HmacSha256(Bytes("k"), Bytes("m"));
    auto b = a;
    EXPECT_TRUE(ConstantTimeEquals(a, b));
    b[31] ^= std::byte{1};
    EXPECT_FALSE(ConstantTimeEquals(a, b));
    EXPECT_FALSE(ConstantTimeEquals(a, std::span{a}.first(31)));
}

}  // namespace
}  // namespace sa::common
