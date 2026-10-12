#pragma once

#include "sa/common/bytes.hpp"

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace sa::common {

namespace detail {
// Cryptographically random bytes; throws std::runtime_error when the OS source fails.
void FillRandomBytes(std::span<std::byte> out);
[[nodiscard]] std::string BytesToHex(std::span<const std::byte> bytes);
[[nodiscard]] std::size_t HashBytes(std::span<const std::byte> bytes) noexcept;
}  // namespace detail

// 16 random bytes minted by a process rather than by the database (design document 14.1):
// process generations, gateway sessions, item instances, kill and request ids. Each use gets its
// own Tag so that a kill id can never be passed where an item id is expected.
template <typename Tag>
class OpaqueId {
public:
    static constexpr std::size_t kBytes = 16;

    constexpr OpaqueId() = default;

    [[nodiscard]] static OpaqueId Generate() {
        OpaqueId id;
        detail::FillRandomBytes(id.bytes_);
        return id;
    }

    [[nodiscard]] static std::optional<OpaqueId> FromBytes(std::span<const std::byte> bytes) {
        if (bytes.size() != kBytes) {
            return std::nullopt;
        }
        OpaqueId id;
        std::ranges::copy(bytes, id.bytes_.begin());
        return id;
    }

    // From a protobuf `bytes` field.
    [[nodiscard]] static std::optional<OpaqueId> FromString(std::string_view wire) {
        return FromBytes(AsBytes(wire));
    }

    [[nodiscard]] std::span<const std::byte, kBytes> bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::string ToWire() const { return ToWireString(bytes_); }  // protobuf bytes
    [[nodiscard]] std::string ToHex() const { return detail::BytesToHex(bytes_); }  // logs
    [[nodiscard]] bool IsNil() const noexcept {
        return std::ranges::all_of(bytes_, [](std::byte b) { return b == std::byte{0}; });
    }

    friend constexpr auto operator<=>(const OpaqueId&, const OpaqueId&) noexcept = default;

private:
    std::array<std::byte, kBytes> bytes_{};
};

}  // namespace sa::common

// Specialising std::hash for a user-defined type is explicitly permitted ([namespace.std]).
template <typename Tag>
struct std::hash<sa::common::OpaqueId<Tag>> {  // NOLINT(cert-dcl58-cpp)
    std::size_t operator()(const sa::common::OpaqueId<Tag>& id) const noexcept {
        return sa::common::detail::HashBytes(id.bytes());
    }
};
