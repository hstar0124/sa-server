#pragma once

#include <chrono>
#include <cstdint>

namespace sa::common {

// Milliseconds since the Unix epoch as carried in protocol messages (Pong, tickets, leases).
[[nodiscard]] inline std::uint64_t UnixMillisNow() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

}  // namespace sa::common
