#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace sa::common {

// Wall time: what crosses machines (ticket validity, Pong, lease expiry). Durations within one
// process use std::chrono::steady_clock instead, which never steps.
using WallClock = std::chrono::system_clock;
using WallTime = WallClock::time_point;

[[nodiscard]] inline WallTime WallNow() {
    return WallClock::now();
}

// Protocol messages carry wall time as milliseconds since the Unix epoch; these two are the only
// conversions to and from that number.
[[nodiscard]] constexpr std::uint64_t ToUnixMillis(WallTime time) noexcept {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch());
    return static_cast<std::uint64_t>(std::max<std::chrono::milliseconds::rep>(0, ms.count()));
}

[[nodiscard]] constexpr WallTime FromUnixMillis(std::uint64_t ms) noexcept {
    return WallTime{std::chrono::duration_cast<WallClock::duration>(
        std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(ms)})};
}

}  // namespace sa::common
