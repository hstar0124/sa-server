#pragma once

#include "sa/zone/sim/fixed_step_scheduler.hpp"
#include "sa/zone/sim/zone.hpp"

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>

#include <chrono>
#include <functional>

namespace sa::zone {

// Drives Zone::Tick() from an Asio timer on the io_context thread. This is the single logical
// thread that owns the zone state (see README "Zone의 한 틱"): Gateway link reads, Backend RPC
// completions and retry timers run on the same io_context, so none of them races a tick.
class TickRunner {
public:
    // Invoked after every simulation tick, on the zone thread; hosts hang snapshot delivery and
    // local bots off it.
    using AfterTick = std::function<void(common::TickIndex)>;

    TickRunner(asio::io_context& io, sim::Zone& zone, sim::FixedStepScheduler scheduler,
               std::chrono::seconds stats_log_interval, AfterTick after_tick);

    void Start();
    void Stop();

private:
    void ScheduleNext();
    void OnTimer();
    void LogStatsIfDue(std::chrono::steady_clock::time_point now);

    asio::steady_timer timer_;
    sim::Zone& zone_;
    sim::FixedStepScheduler scheduler_;
    std::chrono::seconds stats_log_interval_;
    AfterTick after_tick_;
    std::chrono::steady_clock::time_point last_wake_;
    std::chrono::steady_clock::time_point next_stats_;
    std::uint64_t ticks_since_stats_ = 0;
    std::uint64_t dropped_at_last_stats_ = 0;
    bool running_ = false;
};

}  // namespace sa::zone
