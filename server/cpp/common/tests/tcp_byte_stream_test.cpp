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

}  // namespace
}  // namespace sa::common
