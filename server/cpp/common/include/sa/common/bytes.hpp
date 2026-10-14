#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Conversions between std::byte buffers and the char-based containers protobuf and OpenSSL use.
// Kept in one place so the rest of the code never spells a reinterpret_cast.
namespace sa::common {

// Copies bytes into a std::string, e.g. for a protobuf `bytes` field.
[[nodiscard]] std::string ToWireString(std::span<const std::byte> bytes);

// Views a string's contents as bytes (no copy).
[[nodiscard]] inline std::span<const std::byte> AsBytes(std::string_view text) noexcept {
    return std::as_bytes(std::span{text});
}

// Standard base64 with '=' padding; nullopt when the text is not valid base64.
[[nodiscard]] std::optional<std::vector<std::byte>> DecodeBase64(std::string_view text);

}  // namespace sa::common
