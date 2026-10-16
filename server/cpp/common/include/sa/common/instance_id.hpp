#pragma once

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

// 16 random bytes identifying one process generation or one gateway session. A restarted process
// is a new instance even under the same pod name (see README "소유권과 lease").
class InstanceId {
public:
    static constexpr std::size_t kBytes = 16;

    constexpr InstanceId() = default;

    [[nodiscard]] static InstanceId Generate();
    [[nodiscard]] static std::optional<InstanceId> FromBytes(std::span<const std::byte> bytes);
    [[nodiscard]] static std::optional<InstanceId> FromString(
        std::string_view wire);  // protobuf bytes field

    [[nodiscard]] std::span<const std::byte, kBytes> bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::string ToWire() const;  // for protobuf bytes fields
    [[nodiscard]] std::string ToHex() const;   // for logs
    [[nodiscard]] bool IsNil() const noexcept;

    friend constexpr auto operator<=>(const InstanceId&, const InstanceId&) noexcept = default;

private:
    std::array<std::byte, kBytes> bytes_{};
};

}  // namespace sa::common

template <>
struct std::hash<sa::common::InstanceId> {
    std::size_t operator()(const sa::common::InstanceId& id) const noexcept;
};
