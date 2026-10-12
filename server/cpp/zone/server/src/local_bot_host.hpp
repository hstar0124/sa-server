#pragma once

#include "sa/bot/wanderer.hpp"
#include "sa/common/ids.hpp"
#include "sa/zone/sim/zone.hpp"

#include <cstdint>
#include <vector>

namespace sa::zone {

// Development aid: runs bot brains inside the zone process, bypassing the network, so movement
// and interest management can be exercised and measured before the gateway path exists.
// Mirrors what a client does: decide once per tick, consume a view at snapshot cadence.
class LocalBotHost {
public:
    LocalBotHost(sim::Zone& zone, std::uint32_t count, std::uint32_t ticks_per_snapshot,
                 std::uint64_t seed);

    void OnTick(common::TickIndex tick);

    struct Totals {
        std::uint64_t views = 0;
        std::uint64_t spawned = 0;
        std::uint64_t updated = 0;
        std::uint64_t despawned = 0;
    };
    // Cumulative counts; the caller diffs them for periodic logging.
    [[nodiscard]] const Totals& totals() const noexcept { return totals_; }
    [[nodiscard]] std::size_t size() const noexcept { return bots_.size(); }

private:
    struct Bot {
        common::EntityId entity;
        bot::Wanderer brain;
        std::uint32_t next_input_seq = 1;
    };

    sim::Zone& zone_;
    std::uint32_t ticks_per_snapshot_;
    std::vector<Bot> bots_;
    Totals totals_;
};

}  // namespace sa::zone
