#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// RFC 6455 server-side codec: the opening handshake and the data framing. Pure functions and a
// streaming decoder; no sockets. Only what a binary-protobuf game link needs: single-frame
// binary messages plus the control frames the RFC requires every endpoint to handle.
namespace sa::common::websocket {

// An HTTP request bigger than this is not a handshake attempt.
inline constexpr std::size_t kMaxHandshakeBytes = std::size_t{8} * 1024;

struct HandshakeRequest {
    std::string path;
    std::string key;  // Sec-WebSocket-Key, as sent
};

enum class HandshakeError : std::uint8_t {
    kIncomplete,    // header terminator not seen yet
    kTooLarge,      // exceeded kMaxHandshakeBytes without a terminator
    kNotAnUpgrade,  // not "GET ... HTTP/1.1" with Upgrade: websocket
    kBadVersion,    // Sec-WebSocket-Version is not 13
    kMissingKey,
};

// Parses the bytes received so far. kIncomplete asks the caller to read more.
[[nodiscard]] std::expected<HandshakeRequest, HandshakeError> ParseHandshake(
    std::string_view received);

// Sec-WebSocket-Accept for a client key: base64(SHA-1(key + RFC GUID)).
[[nodiscard]] std::string AcceptKey(std::string_view client_key);

[[nodiscard]] std::string HandshakeResponse(std::string_view accept_key);
[[nodiscard]] std::string RejectionResponse();  // 400 for a malformed handshake

enum class Opcode : std::uint8_t {
    kContinuation = 0x0,
    kText = 0x1,
    kBinary = 0x2,
    kClose = 0x8,
    kPing = 0x9,
    kPong = 0xA,
};

struct Frame {
    Opcode opcode = Opcode::kBinary;
    bool fin = true;
    std::vector<std::byte> payload;  // unmasked
};

enum class FrameError : std::uint8_t {
    kReservedBits,  // RSV1-3 set without an extension
    kUnknownOpcode,
    kUnmaskedClientFrame,  // clients must mask (RFC 6455 §5.1)
    kPayloadTooLarge,
    kControlFrameTooLong,  // control payloads are at most 125 bytes
};

// Incremental decoder for frames arriving from a client (masked).
class FrameDecoder {
public:
    explicit FrameDecoder(std::size_t max_payload_bytes) : max_payload_bytes_(max_payload_bytes) {}

    void Feed(std::span<const std::byte> bytes);
    // nullopt when no complete frame is buffered; an error means the connection must close.
    [[nodiscard]] std::expected<std::optional<Frame>, FrameError> TryPop();

private:
    void CompactIfWorthwhile();

    std::size_t max_payload_bytes_;
    std::vector<std::byte> buffer_;
    std::size_t read_pos_ = 0;
};

// Encodes a server frame (never masked) and appends it to `out`.
void EncodeFrame(Opcode opcode, std::span<const std::byte> payload, std::vector<std::byte>& out);

// Close status codes this server sends (RFC 6455 §7.4.1).
enum class CloseStatus : std::uint16_t {
    kNormal = 1000,
    kGoingAway = 1001,
    kProtocolError = 1002,
    kUnsupportedData = 1003,
    kMessageTooBig = 1009,
};
[[nodiscard]] std::vector<std::byte> ClosePayload(CloseStatus status);

}  // namespace sa::common::websocket
