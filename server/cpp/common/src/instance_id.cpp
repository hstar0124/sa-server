#include "sa/common/instance_id.hpp"

#include "sa/common/bytes.hpp"

#include <openssl/rand.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <stdexcept>

namespace sa::common {

// OpenSSL and std::string speak char/unsigned char; these casts are the only bridge from std::byte.
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
InstanceId InstanceId::Generate() {
    InstanceId id;
    if (RAND_bytes(reinterpret_cast<unsigned char*>(id.bytes_.data()), static_cast<int>(kBytes)) !=
        1) {
        throw std::runtime_error{"RAND_bytes failed"};
    }
    return id;
}

std::optional<InstanceId> InstanceId::FromBytes(std::span<const std::byte> bytes) {
    if (bytes.size() != kBytes) {
        return std::nullopt;
    }
    InstanceId id;
    std::ranges::copy(bytes, id.bytes_.begin());
    return id;
}

std::optional<InstanceId> InstanceId::FromString(std::string_view wire) {
    return FromBytes(std::as_bytes(std::span{wire}));
}

std::string InstanceId::ToWire() const {
    return ToWireString(bytes_);
}

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

std::string InstanceId::ToHex() const {
    std::string out;
    out.reserve(kBytes * 2);
    for (const std::byte b : bytes_) {
        out += std::format("{:02x}", std::to_integer<unsigned>(b));
    }
    return out;
}

bool InstanceId::IsNil() const noexcept {
    return std::ranges::all_of(bytes_, [](std::byte b) { return b == std::byte{0}; });
}

}  // namespace sa::common

std::size_t std::hash<sa::common::InstanceId>::operator()(
    const sa::common::InstanceId& id) const noexcept {
    std::uint64_t head = 0;
    std::uint64_t tail = 0;
    const auto bytes = id.bytes();
    std::memcpy(&head, bytes.first<sizeof head>().data(), sizeof head);
    std::memcpy(&tail, bytes.last<sizeof tail>().data(), sizeof tail);
    return std::hash<std::uint64_t>{}(head ^ std::rotl(tail, 29));
}
