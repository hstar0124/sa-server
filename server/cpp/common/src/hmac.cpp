#include "sa/common/hmac.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <stdexcept>

namespace sa::common {

// OpenSSL speaks unsigned char*; the casts below are the only bridge between std::byte and it.
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
HmacSha256Digest HmacSha256(std::span<const std::byte> key, std::span<const std::byte> message) {
    HmacSha256Digest digest{};
    unsigned int written = 0;
    const unsigned char* out =
        ::HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
               reinterpret_cast<const unsigned char*>(message.data()), message.size(),
               reinterpret_cast<unsigned char*>(digest.data()), &written);
    if (out == nullptr || written != kHmacSha256Bytes) {
        throw std::runtime_error{"HMAC-SHA256 failed"};
    }
    return digest;
}
// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

bool ConstantTimeEquals(std::span<const std::byte> a, std::span<const std::byte> b) {
    if (a.size() != b.size()) {
        return false;
    }
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

}  // namespace sa::common
