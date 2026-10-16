#include "sa/common/framing.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <vector>

namespace sa::common {
namespace {

std::vector<std::byte> Bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

TEST(EncodeFrame, WritesBigEndianLengthThenPayload) {
    std::vector<std::byte> out;
    const auto payload = Bytes({0xAA, 0xBB, 0xCC});
    ASSERT_TRUE(EncodeFrame(payload, FrameLimits{}, out).has_value());
    EXPECT_EQ(out, Bytes({0x00, 0x00, 0x00, 0x03, 0xAA, 0xBB, 0xCC}));
}

TEST(EncodeFrame, RejectsEmptyAndOversizedPayload) {
    std::vector<std::byte> out;
    EXPECT_EQ(EncodeFrame({}, FrameLimits{}, out).error(), FrameError::kEmptyPayload);

    const FrameLimits tiny{.max_payload_bytes = 2};
    EXPECT_EQ(EncodeFrame(Bytes({1, 2, 3}), tiny, out).error(), FrameError::kPayloadTooLarge);
    EXPECT_TRUE(out.empty());
}

TEST(FrameDecoder, ReassemblesFramesSplitAcrossReads) {
    FrameDecoder decoder{FrameLimits{}};
    std::vector<std::byte> wire;
    ASSERT_TRUE(EncodeFrame(Bytes({1, 2}), FrameLimits{}, wire).has_value());
    ASSERT_TRUE(EncodeFrame(Bytes({3}), FrameLimits{}, wire).has_value());

    // Feed one byte at a time: header and payload boundaries are all crossed.
    std::vector<std::vector<std::byte>> frames;
    for (std::byte b : wire) {
        decoder.Feed(std::span{&b, 1});
        while (true) {
            auto popped = decoder.TryPop();
            ASSERT_TRUE(popped.has_value());
            if (!*popped) {
                break;
            }
            frames.emplace_back((**popped).begin(), (**popped).end());
        }
    }
    ASSERT_EQ(frames.size(), 2U);
    EXPECT_EQ(frames[0], Bytes({1, 2}));
    EXPECT_EQ(frames[1], Bytes({3}));
    EXPECT_EQ(decoder.buffered_bytes(), 0U);
}

TEST(FrameDecoder, OversizedHeaderIsAnErrorBeforePayloadArrives) {
    FrameDecoder decoder{FrameLimits{.max_payload_bytes = 16}};
    decoder.Feed(Bytes({0x00, 0x00, 0x00, 0x11}));  // declares 17 bytes
    const auto popped = decoder.TryPop();
    ASSERT_FALSE(popped.has_value());
    EXPECT_EQ(popped.error(), FrameError::kPayloadTooLarge);
}

TEST(FrameDecoder, ZeroLengthFrameIsAnError) {
    FrameDecoder decoder{FrameLimits{}};
    decoder.Feed(Bytes({0, 0, 0, 0}));
    EXPECT_EQ(decoder.TryPop().error(), FrameError::kEmptyPayload);
}

TEST(FrameDecoder, CompactsConsumedBytesAcrossManyFrames) {
    FrameDecoder decoder{FrameLimits{}};
    std::vector<std::byte> wire;
    ASSERT_TRUE(EncodeFrame(Bytes({7}), FrameLimits{}, wire).has_value());
    for (int i = 0; i < 10'000; ++i) {
        decoder.Feed(wire);
        auto popped = decoder.TryPop();
        ASSERT_TRUE(popped.has_value() && popped->has_value());
    }
    EXPECT_EQ(decoder.buffered_bytes(), 0U);
}

}  // namespace
}  // namespace sa::common
