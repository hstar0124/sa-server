#include "sa/common/framed_connection.hpp"
#include "sa/common/framing.hpp"
#include "sa/common/testing/loopback.hpp"
#include "sa/common/websocket_connection.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace sa::common {
namespace {

using testing::Loopback;

constexpr std::byte kBinary{0x2};

std::vector<std::byte> TwoFrames() {
    const std::vector<std::byte> payload(3, std::byte{7});
    std::vector<std::byte> wire;
    for (int i = 0; i < 2; ++i) {
        if (!EncodeFrame(payload, FrameLimits{}, wire)) {
            throw std::logic_error{"test frame does not encode"};
        }
    }
    return wire;
}

// The zone link closes its connection from inside a message handler and drops its reference in
// the close callback. The connection must survive until its own read loop has unwound: with a
// second frame still buffered, the loop looks at the stream again after the handler returns.
TEST(FramedConnection, AHandlerThatClosesAndDropsTheConnectionLeavesNothingDangling) {
    Loopback lo;
    auto connection = std::make_shared<FramedConnection>(lo.TakeServerSocket(), FrameLimits{});
    int messages = 0;
    int closed = 0;
    connection->Start(MessageConnection::Callbacks{
        .on_message =
            [&](std::span<const std::byte>) {
                ++messages;
                connection->Abort();
            },
        .on_closed =
            [&](const std::error_code&) {
                ++closed;
                connection.reset();
            },
    });
    lo.WriteFromPeer(TwoFrames());
    lo.RunUntil([&] { return closed > 0; });

    EXPECT_EQ(messages, 1) << "nothing is delivered after the handler closed the connection";
    EXPECT_EQ(closed, 1);
}

TEST(WebSocketConnection, AHandlerThatClosesAndDropsTheConnectionLeavesNothingDangling) {
    Loopback lo;
    auto connection = std::make_shared<WebSocketConnection>(lo.TakeServerSocket(), FrameLimits{});
    int messages = 0;
    int closed = 0;
    connection->Start(MessageConnection::Callbacks{
        .on_message =
            [&](std::span<const std::byte>) {
                ++messages;
                connection->Abort();
            },
        .on_closed =
            [&](const std::error_code&) {
                ++closed;
                connection.reset();
            },
    });
    lo.WriteFromPeer(testing::kWebSocketHandshake);
    std::vector<std::byte> wire;
    const std::vector<std::byte> payload(3, std::byte{7});
    for (int i = 0; i < 2; ++i) {
        testing::AppendClientFrame(wire, kBinary, true, payload);
    }
    lo.WriteFromPeer(wire);
    lo.RunUntil([&] { return closed > 0; });

    EXPECT_EQ(messages, 1);
    EXPECT_EQ(closed, 1);
}

}  // namespace
}  // namespace sa::common
