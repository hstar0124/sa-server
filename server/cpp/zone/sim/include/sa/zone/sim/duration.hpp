#pragma once

#include <chrono>
#include <cstdint>

namespace sa::zone::sim {

using Duration = std::chrono::nanoseconds;

// Simulation frequency expressed as a type so that "20" never floats around as a bare int.
struct TickRate {
    std::uint32_t hz = 0;

    [[nodiscard]] constexpr Duration Period() const noexcept {
        return std::chrono::duration_cast<Duration>(std::chrono::seconds{1}) / hz;
    }
};

}  // namespace sa::zone::sim
