#include "tick_runner.hpp"

#include <spdlog/spdlog.h>

#include <system_error>
#include <utility>

namespace sa::zone {

TickRunner::TickRunner(asio::io_context& io, sim::Zone& zone, sim::FixedStepScheduler scheduler,
                       std::chrono::seconds stats_log_interval, AfterTick after_tick, Clock clock)
    : timer_(io),
      zone_(zone),
      scheduler_(scheduler),
      stats_log_interval_(stats_log_interval),
      after_tick_(std::move(after_tick)),
      clock_(std::move(clock)) {
    // The phases are timed on the same real clock as sim_time_, whatever drives the scheduler.
    zone_.set_profile_clock(&std::chrono::steady_clock::now);
}

void TickRunner::Start() {
    running_ = true;
    last_wake_ = clock_();
    next_stats_ = last_wake_ + stats_log_interval_;
    timer_.expires_at(std::chrono::steady_clock::now());
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
    const auto now = clock_();
    const std::uint32_t due = scheduler_.Advance(now - last_wake_);
    last_wake_ = now;
    for (std::uint32_t i = 0; i < due; ++i) {
        const auto before_sim = std::chrono::steady_clock::now();
        zone_.Tick();
        const auto before_host = std::chrono::steady_clock::now();
        if (after_tick_) {
            after_tick_(zone_.current_tick());
        }
        sim_time_ += before_host - before_sim;
        host_time_ += std::chrono::steady_clock::now() - before_host;
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
    const auto micros = [](std::chrono::nanoseconds d) {
        return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
    };
    spdlog::info(
        "tick={} ran={} dropped={} lag_us={} players={} cmds={} deferred={} refused={} "
        "superseded={} stale={} sim_us={} host_us={}",
        zone_.current_tick().value(), ticks_since_stats_, dropped, micros(scheduler_.lag()),
        zs.players, zs.commands_applied, zs.commands_deferred, zs.commands_dropped,
        zs.moves_superseded, zs.stale_inputs_dropped, micros(sim_time_), micros(host_time_));
    // The simulation step broken into its phases, to show which one a load change moved.
    const sim::TickProfile phases = zone_.TakeProfile();
    spdlog::info(
        "phases commands_us={} casts_us={} players_us={} monsters_us={} projectiles_us={} "
        "upkeep_us={} interest_us={}",
        micros(phases.commands), micros(phases.casts), micros(phases.players),
        micros(phases.monsters), micros(phases.projectiles), micros(phases.upkeep),
        micros(phases.interest));
    sim_time_ = {};
    host_time_ = {};
    if (on_stats_) {
        on_stats_();
    }
    ticks_since_stats_ = 0;
    dropped_at_last_stats_ = scheduler_.dropped_ticks();
    next_stats_ = now + stats_log_interval_;
}

}  // namespace sa::zone
