#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sa::common {

// HMAC-SHA256 over OpenSSL. Used to verify entry tickets (ADR-0005); the signing side is the
// Backend. Constant-time comparison so a forged signature cannot be refined byte by byte.
inline constexpr std::size_t kHmacSha256Bytes = 32;
using HmacSha256Digest = std::array<std::byte, kHmacSha256Bytes>;

[[nodiscard]] HmacSha256Digest HmacSha256(std::span<const std::byte> key,
                                          std::span<const std::byte> message);

[[nodiscard]] bool ConstantTimeEquals(std::span<const std::byte> a, std::span<const std::byte> b);

}  // namespace sa::common
