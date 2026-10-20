#include "sa/common/bytes.hpp"
#include "sa/common/framed_connection.hpp"
#include "sa/common/framing.hpp"
#include "sa/common/testing/loopback.hpp"
#include "sa/common/websocket_connection.hpp"

#include <asio/error.hpp>
#include <gtest/gtest.h>

#include <memory>
#include <span>
#include <stdexcept>
#include <string>
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
    auto connection =
        std::make_shared<FramedConnection>(lo.TakeServerSocket(), FrameLimits{}, std::nullopt);
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
    auto connection =
        std::make_shared<WebSocketConnection>(lo.TakeServerSocket(), FrameLimits{}, std::nullopt);
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

constexpr std::byte kClose{0x8};
constexpr std::byte kPing{0x9};
constexpr std::byte kPongFin{0x8A};
constexpr std::byte kCloseFin{0x88};

struct WebSocketPeer {
    Loopback lo;
    std::shared_ptr<WebSocketConnection> connection =
        std::make_shared<WebSocketConnection>(lo.TakeServerSocket(), FrameLimits{}, std::nullopt);
    std::vector<std::vector<std::byte>> messages;
    bool closed = false;
    std::error_code reason;

    WebSocketPeer(const WebSocketPeer&) = delete;
    WebSocketPeer& operator=(const WebSocketPeer&) = delete;
    WebSocketPeer(WebSocketPeer&&) = delete;
    WebSocketPeer& operator=(WebSocketPeer&&) = delete;

    // The connection and its byte stream hold each other until the stream finishes; a test that
    // left it open would leak the pair. Abort finishes the stream before it returns.
    ~WebSocketPeer() { connection->Abort(); }

    WebSocketPeer() {
        connection->Start(MessageConnection::Callbacks{
            .on_message =
                [this](std::span<const std::byte> payload) {
                    messages.emplace_back(payload.begin(), payload.end());
                },
            .on_closed =
                [this](const std::error_code& ec) {
                    closed = true;
                    reason = ec;
                },
        });
    }
};

TEST(WebSocketConnection, AFrameSentWithTheHandshakeIsDelivered) {
    WebSocketPeer ws;
    std::vector<std::byte> wire;
    const auto handshake = AsBytes(testing::kWebSocketHandshake);
    wire.insert(wire.end(), handshake.begin(), handshake.end());
    const std::vector<std::byte> payload{std::byte{1}, std::byte{2}, std::byte{3}};
    testing::AppendClientFrame(wire, kBinary, true, payload);
    ws.lo.WriteFromPeer(wire);  // one write: the frame shares a packet with the request
    ws.lo.RunUntil([&] { return !ws.messages.empty(); });
    EXPECT_EQ(ws.messages.front(), payload);
}

TEST(WebSocketConnection, APingIsAnsweredWithAPongCarryingItsPayload) {
    WebSocketPeer ws;
    ws.lo.WriteFromPeer(testing::kWebSocketHandshake);
    std::vector<std::byte> ping;
    const std::vector<std::byte> payload{std::byte{'h'}, std::byte{'i'}};
    testing::AppendClientFrame(ping, kPing, true, payload);
    ws.lo.WriteFromPeer(ping);
    ws.lo.RunUntil([&] { return ws.lo.peer().available() > 0; });

    EXPECT_NE(ws.lo.ReadHttpHeadAtPeer().find("101 Switching Protocols"), std::string::npos);
    ws.lo.RunUntil([&] { return ws.lo.peer().available() >= 4; });
    const std::vector<std::byte> expected{kPongFin, std::byte{2}, std::byte{'h'}, std::byte{'i'}};
    EXPECT_EQ(ws.lo.ReadExactlyAtPeer(4), expected);
}

TEST(WebSocketConnection, AClientCloseIsEchoedAndTheConnectionEndsInOrder) {
    WebSocketPeer ws;
    ws.lo.WriteFromPeer(testing::kWebSocketHandshake);
    std::vector<std::byte> close;
    const std::vector<std::byte> normal{std::byte{0x03}, std::byte{0xE8}};  // status 1000
    testing::AppendClientFrame(close, kClose, true, normal);
    ws.lo.WriteFromPeer(close);
    ws.lo.RunUntil([&] { return ws.lo.peer().available() > 0; });
    (void)ws.lo.ReadHttpHeadAtPeer();

    const auto read = ws.lo.ReadToEndAtPeer();
    const std::vector<std::byte> echo{kCloseFin, std::byte{2}, std::byte{0x03}, std::byte{0xE8}};
    EXPECT_EQ(read.bytes, echo);
    EXPECT_EQ(read.end, asio::error::eof);

    ws.lo.peer().close();
    ws.lo.RunUntil([&] { return ws.closed; });
    EXPECT_FALSE(ws.reason);
}

}  // namespace
}  // namespace sa::common
