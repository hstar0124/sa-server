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
    // Where the runner reads the time it has fallen behind. The timer still wakes it on the real
    // clock; tests substitute this to simulate a stall without waiting through one.
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    TickRunner(asio::io_context& io, sim::Zone& zone, sim::FixedStepScheduler scheduler,
               std::chrono::seconds stats_log_interval, AfterTick after_tick,
               Clock clock = &std::chrono::steady_clock::now);

    [[nodiscard]] std::uint64_t dropped_ticks() const noexcept {
        return scheduler_.dropped_ticks();
    }

    void Start();
    void Stop();
    // Runs right after the periodic statistics line, for the host's own line.
    void set_on_stats(std::function<void()> fn) { on_stats_ = std::move(fn); }

private:
    void ScheduleNext();
    void OnTimer();
    void LogStatsIfDue(std::chrono::steady_clock::time_point now);

    asio::steady_timer timer_;
    sim::Zone& zone_;
    sim::FixedStepScheduler scheduler_;
    std::chrono::seconds stats_log_interval_;
    AfterTick after_tick_;
    std::function<void()> on_stats_;
    Clock clock_;
    std::chrono::steady_clock::time_point last_wake_;
    std::chrono::steady_clock::time_point next_stats_;
    std::uint64_t ticks_since_stats_ = 0;
    std::uint64_t dropped_at_last_stats_ = 0;
    // Where the zone thread's time went since the last statistics line: the simulation step and
    // the host's delivery after it (views, encoding, link writes, Backend calls). Against the
    // window they show how close the single thread is to saturation, and which half fills it.
    std::chrono::steady_clock::duration sim_time_{};
    std::chrono::steady_clock::duration host_time_{};
    bool running_ = false;
};

}  // namespace sa::zone
