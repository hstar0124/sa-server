#include "sa/common/framing.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace sa::common {
namespace {

constexpr std::size_t kHeaderBytes = protocol::kFrameHeaderBytes;
static_assert(kHeaderBytes == sizeof(std::uint32_t), "header is a uint32 length");

std::uint32_t ReadBigEndianU32(std::span<const std::byte, kHeaderBytes> bytes) {
    std::uint32_t value = 0;
    std::memcpy(&value, bytes.data(), kHeaderBytes);
    if constexpr (std::endian::native == std::endian::little) {
        value = std::byteswap(value);
    }
    return value;
}

void WriteBigEndianU32(std::uint32_t value, std::span<std::byte, kHeaderBytes> out) {
    if constexpr (std::endian::native == std::endian::little) {
        value = std::byteswap(value);
    }
    std::memcpy(out.data(), &value, kHeaderBytes);
}

}  // namespace

std::expected<void, FrameError> EncodeFrame(std::span<const std::byte> payload,
                                            const FrameLimits& limits,
                                            std::vector<std::byte>& out) {
    if (payload.empty()) {
        return std::unexpected(FrameError::kEmptyPayload);
    }
    if (payload.size() > limits.max_payload_bytes) {
        return std::unexpected(FrameError::kPayloadTooLarge);
    }
    const std::size_t start = out.size();
    out.resize(start + kHeaderBytes + payload.size());
    const std::span<std::byte> frame = std::span{out}.subspan(start);
    WriteBigEndianU32(static_cast<std::uint32_t>(payload.size()), frame.first<kHeaderBytes>());
    std::ranges::copy(payload, frame.subspan(kHeaderBytes).begin());
    return {};
}

void FrameDecoder::Feed(std::span<const std::byte> bytes) {
    CompactIfWorthwhile();
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

std::expected<std::optional<std::span<const std::byte>>, FrameError> FrameDecoder::TryPop() {
    if (buffered_bytes() < kHeaderBytes) {
        return std::nullopt;
    }
    const std::span<const std::byte> unread = std::span{buffer_}.subspan(read_pos_);
    const std::size_t payload_len = ReadBigEndianU32(unread.first<kHeaderBytes>());
    if (payload_len == 0) {
        return std::unexpected(FrameError::kEmptyPayload);
    }
    if (payload_len > limits_.max_payload_bytes) {
        return std::unexpected(FrameError::kPayloadTooLarge);
    }
    if (buffered_bytes() < kHeaderBytes + payload_len) {
        return std::nullopt;
    }
    const std::span<const std::byte> payload = unread.subspan(kHeaderBytes, payload_len);
    read_pos_ += kHeaderBytes + payload_len;
    return payload;
}

void FrameDecoder::CompactIfWorthwhile() {
    // Drop consumed bytes once they dominate the buffer; amortises the memmove.
    if (read_pos_ == 0) {
        return;
    }
    if (read_pos_ == buffer_.size() || read_pos_ > buffer_.size() / 2) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(read_pos_));
        read_pos_ = 0;
    }
}

}  // namespace sa::common
