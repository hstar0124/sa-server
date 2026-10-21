#include "sa/common/websocket.hpp"

#include "sa/common/bytes.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace sa::common::websocket {
namespace {

// The frame a decoder just produced; "nothing yet" or an error is a test bug here.
Frame FrameOf(const std::expected<std::optional<Frame>, FrameError>& popped) {
    if (!popped.has_value() || !popped->has_value()) {
        throw std::logic_error{"expected a complete frame"};
    }
    return **popped;
}

std::vector<std::byte> Bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

constexpr std::string_view kBrowserHandshake =
    "GET /game HTTP/1.1\r\n"
    "Host: localhost:7778\r\n"
    "Upgrade: websocket\r\n"
    "Connection: keep-alive, Upgrade\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n"
    "\r\n";

TEST(Handshake, AcceptKeyMatchesRfc6455Example) {
    EXPECT_EQ(AcceptKey("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(Handshake, ParsesABrowserRequestAndReportsIncompleteUntilTheTerminator) {
    const auto partial = ParseHandshake(kBrowserHandshake.substr(0, 40));
    EXPECT_EQ(partial.error(), HandshakeError::kIncomplete);

    const auto request = ParseHandshake(kBrowserHandshake);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->path, "/game");
    EXPECT_EQ(request->key, "dGhlIHNhbXBsZSBub25jZQ==");
    EXPECT_NE(HandshakeResponse(AcceptKey(request->key)).find("101 Switching Protocols"),
              std::string::npos);
}

TEST(Handshake, RejectsPlainHttpWrongVersionAndMissingKey) {
    EXPECT_EQ(ParseHandshake("GET / HTTP/1.1\r\nHost: x\r\n\r\n").error(),
              HandshakeError::kNotAnUpgrade);
    EXPECT_EQ(
        ParseHandshake("POST /game HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n")
            .error(),
        HandshakeError::kNotAnUpgrade);
    std::string bad_version{kBrowserHandshake};
    bad_version.replace(bad_version.find("Version: 13"), 11, "Version: 8");
    EXPECT_EQ(ParseHandshake(bad_version).error(), HandshakeError::kBadVersion);
    std::string no_key{kBrowserHandshake};
    no_key.erase(no_key.find("Sec-WebSocket-Key"),
                 std::string("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n").size());
    EXPECT_EQ(ParseHandshake(no_key).error(), HandshakeError::kMissingKey);
    EXPECT_EQ(ParseHandshake(std::string(kMaxHandshakeBytes, 'x')).error(),
              HandshakeError::kTooLarge);
}

TEST(FrameDecoder, DecodesTheRfcMaskedHelloExample) {
    FrameDecoder decoder{1024};
    // RFC 6455 §5.7: single-frame masked text "Hello". Our server treats text as data too.
    decoder.Feed(Bytes({0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58}));
    const Frame frame = FrameOf(decoder.TryPop());
    EXPECT_EQ(frame.opcode, Opcode::kText);
    EXPECT_TRUE(frame.fin);
    EXPECT_EQ(ToWireString(frame.payload), "Hello");
}

TEST(FrameDecoder, ReassemblesAcrossFeedsAndHandlesExtendedLengths) {
    FrameDecoder decoder{70000};
    std::vector<std::byte> payload(300, std::byte{0x41});
    // Build a masked client frame by hand: binary, 16-bit length, mask of zeros (identity).
    std::vector<std::byte> wire = Bytes({0x82, 0xFE, 0x01, 0x2C, 0x00, 0x00, 0x00, 0x00});
    wire.insert(wire.end(), payload.begin(), payload.end());
    for (std::size_t i = 0; i < wire.size(); i += 7) {
        decoder.Feed(std::span{wire}.subspan(i, std::min<std::size_t>(7, wire.size() - i)));
    }
    const Frame frame = FrameOf(decoder.TryPop());
    EXPECT_EQ(frame.opcode, Opcode::kBinary);
    EXPECT_EQ(frame.payload.size(), 300U);

    // 64-bit length form for a 70000-byte payload.
    std::vector<std::byte> big = Bytes({0x82, 0xFF, 0, 0, 0, 0, 0, 0x01, 0x11, 0x70, 0, 0, 0, 0});
    big.insert(big.end(), 70000, std::byte{0x01});
    decoder.Feed(big);
    EXPECT_EQ(FrameOf(decoder.TryPop()).payload.size(), 70000U);
}

TEST(FrameDecoder, RejectsUnmaskedReservedOversizedAndLongControlFrames) {
    FrameDecoder decoder{16};
    decoder.Feed(Bytes({0x82, 0x01, 0x00}));  // mask bit clear
    EXPECT_EQ(decoder.TryPop().error(), FrameError::kUnmaskedClientFrame);

    FrameDecoder rsv{16};
    rsv.Feed(Bytes({0xC2, 0x80}));
    EXPECT_EQ(rsv.TryPop().error(), FrameError::kReservedBits);

    FrameDecoder big{16};
    big.Feed(Bytes({0x82, 0x91}));  // 17 bytes > 16
    EXPECT_EQ(big.TryPop().error(), FrameError::kPayloadTooLarge);

    FrameDecoder control{1024};
    control.Feed(Bytes({0x89, 0xFE, 0x00, 0x7E}));  // ping with 126-byte payload
    EXPECT_EQ(control.TryPop().error(), FrameError::kControlFrameTooLong);

    FrameDecoder unknown{16};
    unknown.Feed(Bytes({0x83, 0x80}));  // opcode 3 is reserved
    EXPECT_EQ(unknown.TryPop().error(), FrameError::kUnknownOpcode);
}

TEST(FrameDecoder, RejectsFragmentedControlFrames) {
    FrameDecoder decoder{16};
    decoder.Feed(Bytes({0x09, 0x80, 0, 0, 0, 0}));  // ping, FIN clear, empty, zero mask
    EXPECT_EQ(decoder.TryPop().error(), FrameError::kFragmentedControlFrame);
}

TEST(EncodeFrame, ProducesUnmaskedServerFramesWithTheRightLengthForm) {
    std::vector<std::byte> out;
    EncodeFrame(Opcode::kBinary, AsBytes("Hello"), out);
    EXPECT_EQ(out, Bytes({0x82, 0x05, 'H', 'e', 'l', 'l', 'o'}));

    out.clear();
    const std::vector<std::byte> mid(126, std::byte{0});
    EncodeFrame(Opcode::kBinary, mid, out);
    EXPECT_EQ(std::to_integer<int>(out[1]), 126);
    EXPECT_EQ(out.size(), 4U + 126U);

    out.clear();
    const std::vector<std::byte> big(70000, std::byte{0});
    EncodeFrame(Opcode::kBinary, big, out);
    EXPECT_EQ(std::to_integer<int>(out[1]), 127);
    EXPECT_EQ(out.size(), 10U + 70000U);

    EXPECT_EQ(ClosePayload(CloseStatus::kProtocolError), Bytes({0x03, 0xEA}));
}

}  // namespace
}  // namespace sa::common::websocket
