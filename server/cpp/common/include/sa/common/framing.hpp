#pragma once

#include "sa/common/protocol_limits.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

// Length-prefixed framing for the game link (ADR-0004):
//   [uint32 big-endian payload length][payload]
// Pure byte manipulation: no sockets, no protobuf parsing.
namespace sa::common {

enum class FrameError : std::uint8_t {
    kPayloadTooLarge,  // declared length exceeds the configured maximum
    kEmptyPayload,     // zero-length frames are not part of the protocol
};

struct FrameLimits {
    std::size_t max_payload_bytes = protocol::kMaxPayloadBytes;
};

// Appends one frame (header + payload) to `out`.
[[nodiscard]] std::expected<void, FrameError> EncodeFrame(std::span<const std::byte> payload,
                                                          const FrameLimits& limits,
                                                          std::vector<std::byte>& out);

// Incremental decoder. Feed() bytes as they arrive, then drain with TryPop() until it yields
// nullopt. A returned view stays valid until the next Feed().
class FrameDecoder {
public:
    explicit FrameDecoder(FrameLimits limits) : limits_(limits) {}

    void Feed(std::span<const std::byte> bytes);

    // nullopt when no complete frame is buffered; an error when the stream is malformed, after
    // which the connection must be closed (the decoder cannot resynchronise).
    [[nodiscard]] std::expected<std::optional<std::span<const std::byte>>, FrameError> TryPop();

    [[nodiscard]] std::size_t buffered_bytes() const noexcept { return buffer_.size() - read_pos_; }

private:
    void CompactIfWorthwhile();

    FrameLimits limits_;
    std::vector<std::byte> buffer_;
    std::size_t read_pos_ = 0;
};

}  // namespace sa::common
