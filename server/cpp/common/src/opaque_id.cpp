#include "sa/common/opaque_id.hpp"

#include <openssl/rand.h>

#include <bit>
#include <cstring>
#include <format>
#include <stdexcept>

namespace sa::common::detail {

void FillRandomBytes(std::span<std::byte> out) {
    // OpenSSL speaks unsigned char; this cast is the only bridge from std::byte.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    if (RAND_bytes(reinterpret_cast<unsigned char*>(out.data()), static_cast<int>(out.size())) !=
        1) {
        throw std::runtime_error{"RAND_bytes failed"};
    }
}

std::string BytesToHex(std::span<const std::byte> bytes) {
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::byte b : bytes) {
        out += std::format("{:02x}", std::to_integer<unsigned>(b));
    }
    return out;
}

std::size_t HashBytes(std::span<const std::byte> bytes) noexcept {
    std::uint64_t head = 0;
    std::uint64_t tail = 0;
    if (bytes.size() >= sizeof head + sizeof tail) {
        std::memcpy(&head, bytes.first<sizeof head>().data(), sizeof head);
        std::memcpy(&tail, bytes.last<sizeof tail>().data(), sizeof tail);
    }
    return std::hash<std::uint64_t>{}(head ^ std::rotl(tail, 29));
}

}  // namespace sa::common::detail
