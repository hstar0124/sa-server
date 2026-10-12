#include "sa/common/lease_keeper.hpp"

#include <spdlog/spdlog.h>

namespace sa::common {

LeaseKeeper::LeaseKeeper(asio::io_context& io, std::string name, LeaseKeeperConfig config,
                         RenewFn renew)
    : name_(std::move(name)),
      config_(config),
      renew_(std::move(renew)),
      lease_(config.safety_margin),
      timer_(io) {}

void LeaseKeeper::Start() {
    running_ = true;
    Renew();
}

void LeaseKeeper::Stop() {
    running_ = false;
    timer_.cancel();
}

bool LeaseKeeper::IsAlive() const {
    return lease_.IsAlive(LeaseState::Clock::now());
}

std::chrono::milliseconds LeaseKeeper::DurationFromServerExpiry(std::uint64_t expires_at_ms,
                                                                std::uint64_t sent_at_ms) {
    // Latency only ever shortens what we believe we hold.
    return std::chrono::milliseconds{expires_at_ms > sent_at_ms ? expires_at_ms - sent_at_ms : 0};
}

void LeaseKeeper::Renew() {
    const auto sent_at = LeaseState::Clock::now();
    renew_([this, sent_at](RenewResult result) {
        if (!running_) {
            return;
        }
        if (result) {
            lease_.Renewed(sent_at, *result);
            if (!was_alive_) {
                spdlog::info("{} lease acquired, valid for {} ms", name_, result->count());
            }
        } else {
            spdlog::warn("{} lease renew failed", name_);
        }
        CheckTransition();
        ScheduleNext();
    });
}

void LeaseKeeper::CheckTransition() {
    const bool alive = IsAlive();
    if (was_alive_ && !alive) {
        spdlog::critical("{} lease lost", name_);
        if (on_lost_) {
            on_lost_();
        }
    }
    was_alive_ = alive;
}

void LeaseKeeper::ScheduleNext() {
    timer_.expires_after(config_.renew_interval);
    timer_.async_wait([this](const std::error_code& ec) {
        if (ec || !running_) {
            return;
        }
        CheckTransition();
        Renew();
    });
}

}  // namespace sa::common
