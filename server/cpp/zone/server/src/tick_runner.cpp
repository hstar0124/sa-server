#include "tick_runner.hpp"

#include <spdlog/spdlog.h>

#include <system_error>
#include <utility>

namespace sa::zone {

TickRunner::TickRunner(asio::io_context& io, sim::Zone& zone, sim::FixedStepScheduler scheduler,
                       std::chrono::seconds stats_log_interval, AfterTick after_tick)
    : timer_(io),
      zone_(zone),
      scheduler_(scheduler),
      stats_log_interval_(stats_log_interval),
      after_tick_(std::move(after_tick)) {}

void TickRunner::Start() {
    running_ = true;
    last_wake_ = std::chrono::steady_clock::now();
    next_stats_ = last_wake_ + stats_log_interval_;
    timer_.expires_at(last_wake_);
    ScheduleNext();
}

void TickRunner::Stop() {
    running_ = false;
    timer_.cancel();
}

void TickRunner::ScheduleNext() {
    // Wake exactly one period after the previous deadline, not after the previous wake, so
    // timer jitter does not accumulate into drift.
    timer_.expires_at(timer_.expiry() + scheduler_.period());
    timer_.async_wait([this](const std::error_code& ec) {
        if (ec == asio::error::operation_aborted || !running_) {
            return;
        }
        OnTimer();
    });
}

void TickRunner::OnTimer() {
    const auto now = std::chrono::steady_clock::now();
    const std::uint32_t due = scheduler_.Advance(now - last_wake_);
    last_wake_ = now;
    for (std::uint32_t i = 0; i < due; ++i) {
        zone_.Tick();
        if (after_tick_) {
            after_tick_(zone_.current_tick());
        }
    }
    ticks_since_stats_ += due;
    LogStatsIfDue(now);
    ScheduleNext();
}

void TickRunner::LogStatsIfDue(std::chrono::steady_clock::time_point now) {
    if (now < next_stats_) {
        return;
    }
    const std::uint64_t dropped = scheduler_.dropped_ticks() - dropped_at_last_stats_;
    const sim::ZoneStats& zs = zone_.stats();
    spdlog::info(
        "tick={} ran={} dropped={} lag_us={} players={} cmds={} deferred={} refused={} "
        "superseded={} stale={}",
        zone_.current_tick().value(), ticks_since_stats_, dropped,
        std::chrono::duration_cast<std::chrono::microseconds>(scheduler_.lag()).count(), zs.players,
        zs.commands_applied, zs.commands_deferred, zs.commands_dropped, zs.moves_superseded,
        zs.stale_inputs_dropped);
    ticks_since_stats_ = 0;
    dropped_at_last_stats_ = scheduler_.dropped_ticks();
    next_stats_ = now + stats_log_interval_;
}

}  // namespace sa::zone
