#include "sa/common/bytes.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <cstring>

namespace sa::common {

std::string ToWireString(std::span<const std::byte> bytes) {
    std::string out(bytes.size(), char{});
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return out;
}

std::optional<std::vector<std::byte>> DecodeBase64(std::string_view text) {
    if (text.empty() || text.size() % 4 != 0) {
        return std::nullopt;
    }
    std::vector<std::byte> out(text.size());
    // OpenSSL takes unsigned char*; this is the only bridge from std::byte in the codebase.
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    const int written = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                                        reinterpret_cast<const unsigned char*>(text.data()),
                                        static_cast<int>(text.size()));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    if (written < 0) {
        return std::nullopt;
    }
    // EVP_DecodeBlock counts '=' padding as data; trim it.
    const auto padding =
        static_cast<std::size_t>(std::ranges::count(text.substr(text.size() - 2), '='));
    out.resize(static_cast<std::size_t>(written) - padding);
    return out;
}

std::string EncodeBase64(std::span<const std::byte> bytes) {
    // 4 output chars per 3 input bytes, rounded up, plus the terminating NUL EVP writes.
    std::string out((((bytes.size() + 2) / 3) * 4) + 1, char{});
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    const int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                                        reinterpret_cast<const unsigned char*>(bytes.data()),
                                        static_cast<int>(bytes.size()));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    out.resize(static_cast<std::size_t>(written));
    return out;
}

}  // namespace sa::common
