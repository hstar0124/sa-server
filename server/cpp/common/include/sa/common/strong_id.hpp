#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ostream>

namespace sa::common {

// Integer wrapped in a distinct type so that a CharacterId cannot be passed where an
// ItemInstanceId is expected. Zero is reserved as the "none" value.
template <typename Tag, typename Rep = std::uint64_t>
class StrongId {
public:
    using rep = Rep;

    constexpr StrongId() noexcept = default;
    constexpr explicit StrongId(Rep value) noexcept : value_(value) {}

    [[nodiscard]] constexpr Rep value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool IsNone() const noexcept { return value_ == Rep{}; }

    // Successor; meaningful for monotonic counters such as OwnerEpoch, harmless for ids.
    [[nodiscard]] constexpr StrongId Next() const noexcept { return StrongId{value_ + 1}; }

    friend constexpr auto operator<=>(StrongId, StrongId) noexcept = default;

    friend std::ostream& operator<<(std::ostream& os, StrongId id) { return os << id.value_; }

private:
    Rep value_{};
};

}  // namespace sa::common

// Specialising std::hash for a user-defined type is explicitly permitted ([namespace.std]).
template <typename Tag, typename Rep>
struct std::hash<sa::common::StrongId<Tag, Rep>> {  // NOLINT(cert-dcl58-cpp)
    std::size_t operator()(sa::common::StrongId<Tag, Rep> id) const noexcept {
        return std::hash<Rep>{}(id.value());
    }
};
