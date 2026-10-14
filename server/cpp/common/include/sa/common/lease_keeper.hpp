#pragma once

#include "sa/common/lease.hpp"

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace sa::common {

struct LeaseKeeperConfig {
    std::chrono::milliseconds renew_interval;
    std::chrono::milliseconds safety_margin;
};

// Periodically renews a Backend lease and tracks whether it is alive (design document 14.3).
// The RPC itself is supplied by the owner, so the same keeper serves zones and gateways.
class LeaseKeeper {
public:
    // Granted duration measured from the moment the request was sent, or nullopt on failure.
    using RenewResult = std::optional<std::chrono::milliseconds>;
    using Done = std::function<void(RenewResult)>;
    using RenewFn = std::function<void(Done)>;

    LeaseKeeper(asio::io_context& io, std::string name, LeaseKeeperConfig config, RenewFn renew);

    void Start();
    void Stop();

    [[nodiscard]] bool IsAlive() const;
    [[nodiscard]] bool ever_acquired() const noexcept { return lease_.ever_renewed(); }
    // Fires once per transition from alive to not alive.
    void set_on_lost(std::function<void()> fn) { on_lost_ = std::move(fn); }

    // Helper for adapters: server expiry (Unix ms) -> duration from our send time.
    [[nodiscard]] static std::chrono::milliseconds DurationFromServerExpiry(
        std::uint64_t expires_at_ms, std::uint64_t sent_at_ms);

private:
    void Renew();
    void ScheduleNext();
    void CheckTransition();

    std::string name_;
    LeaseKeeperConfig config_;
    RenewFn renew_;
    LeaseState lease_;
    asio::steady_timer timer_;
    std::function<void()> on_lost_;
    bool was_alive_ = false;
    bool running_ = false;
};

}  // namespace sa::common
