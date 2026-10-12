#pragma once

#include <chrono>
#include <cstdint>

namespace sa::common {

// Tracks a lease granted by the Backend (design document 14.3). Expiry is interpreted
// conservatively: the holder subtracts a safety margin for clock skew and request latency, and
// treats "never renewed" as not alive.
class LeaseState {
public:
    using Clock = std::chrono::steady_clock;

    explicit LeaseState(std::chrono::milliseconds safety_margin) : margin_(safety_margin) {}

    // Records a successful renewal that the server says lasts `duration` from now.
    void Renewed(Clock::time_point now, std::chrono::milliseconds duration) {
        expires_at_ = now + duration - margin_;
        ever_renewed_ = true;
    }

    [[nodiscard]] bool IsAlive(Clock::time_point now) const noexcept {
        return ever_renewed_ && now < expires_at_;
    }
    [[nodiscard]] bool ever_renewed() const noexcept { return ever_renewed_; }

private:
    std::chrono::milliseconds margin_;
    Clock::time_point expires_at_;
    bool ever_renewed_ = false;
};

}  // namespace sa::common
