#include "sa/common/tcp_byte_stream.hpp"

#include "sa/common/testing/loopback.hpp"

#include <asio/error.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

namespace sa::common {
namespace {

using testing::Loopback;

constexpr std::chrono::milliseconds kTestLinger{50};

std::vector<std::byte> Filled(std::size_t size, std::byte value) {
    return std::vector<std::byte>(size, value);
}

struct Observed {
    bool closed = false;
    std::error_code reason;
};

std::shared_ptr<TcpByteStream> Started(Loopback& lo, Observed& observed,
                                       std::optional<std::chrono::milliseconds> linger) {
    auto stream = std::make_shared<TcpByteStream>(lo.TakeServerSocket(), linger);
    stream->Start(TcpByteStream::Callbacks{
        .on_bytes = [](std::span<const std::byte>) {},
        .on_closed =
            [&observed](const std::error_code& reason) {
                observed.closed = true;
                observed.reason = reason;
            },
    });
    return stream;
}

TEST(TcpByteStream, CloseDeliversQueuedBytesAndEndsOnceThePeerClosesToo) {
    Loopback lo;
    Observed observed;
    auto stream = Started(lo, observed, std::nullopt);
    stream->Write(Filled(300, std::byte{1}));
    stream->Close();
    lo.RunUntil([&] { return lo.peer().available() >= 300; });

    const auto read = lo.ReadToEndAtPeer();
    EXPECT_EQ(read.bytes.size(), 300U);
    EXPECT_EQ(read.end, asio::error::eof) << "our side closed in order";
    EXPECT_FALSE(observed.closed) << "still waiting for the peer to close its side";

    lo.peer().close();
    lo.RunUntil([&] { return observed.closed; });
    EXPECT_FALSE(observed.reason) << "an orderly close";
}

TEST(TcpByteStream, ACloseThePeerNeverCompletesEndsAtTheLinger) {
    Loopback lo;
    Observed observed;
    auto stream = Started(lo, observed, kTestLinger);
    stream->Close();  // the peer neither reads nor closes
    lo.RunUntil([&] { return observed.closed; });
    EXPECT_EQ(observed.reason, std::make_error_code(std::errc::connection_aborted));
}

TEST(TcpByteStream, CountsQueuedBytesUntilTheKernelTakesThem) {
    Loopback lo;
    Observed observed;
    auto stream = Started(lo, observed, std::nullopt);
    EXPECT_EQ(stream->QueuedBytes(), 0U);
    stream->Write(Filled(100, std::byte{1}));
    stream->Write(Filled(200, std::byte{2}));
    stream->Write(Filled(300, std::byte{3}));
    EXPECT_EQ(stream->QueuedBytes(), 600U) << "nothing has run yet";

    const auto bytes = lo.ReadExactlyAtPeer(600);
    lo.RunUntil([&] { return stream->QueuedBytes() == 0; });
    EXPECT_EQ(bytes[0], std::byte{1});
    EXPECT_EQ(bytes[100], std::byte{2});
    EXPECT_EQ(bytes[300], std::byte{3});
    EXPECT_EQ(bytes[599], std::byte{3});
}

// More than the socket buffers hold: the first write cannot finish at once, so the frames queued
// behind it leave as gathered writes, still in order and fully accounted for.
TEST(TcpByteStream, FramesQueuedBehindAWriteLeaveTogetherAndInOrder) {
    Loopback lo;
    Observed observed;
    auto stream = Started(lo, observed, std::nullopt);
    constexpr std::size_t kFrames = 500;
    constexpr std::size_t kFrameBytes = 1000;
    constexpr std::size_t kPatterns = 251;  // a prime, so neighbouring frames differ
    for (std::size_t i = 0; i < kFrames; ++i) {
        stream->Write(Filled(kFrameBytes, static_cast<std::byte>(i % kPatterns)));
    }
    EXPECT_EQ(stream->QueuedBytes(), kFrames * kFrameBytes);

    const auto received = lo.ReadExactlyAtPeer(kFrames * kFrameBytes);
    for (std::size_t i = 0; i < kFrames; ++i) {
        ASSERT_EQ(received[i * kFrameBytes], static_cast<std::byte>(i % kPatterns))
            << "frame " << i;
    }
    lo.RunUntil([&] { return stream->QueuedBytes() == 0; });
}

TEST(TcpByteStream, AbortDropsWhatIsQueued) {
    Loopback lo;
    Observed observed;
    auto stream = Started(lo, observed, std::nullopt);
    stream->Write(Filled(10, std::byte{7}));
    stream->Abort();
    EXPECT_TRUE(observed.closed);
    EXPECT_EQ(observed.reason, std::make_error_code(std::errc::connection_aborted));
    EXPECT_EQ(stream->QueuedBytes(), 0U);
    EXPECT_FALSE(stream->is_open());
}

}  // namespace
}  // namespace sa::common
