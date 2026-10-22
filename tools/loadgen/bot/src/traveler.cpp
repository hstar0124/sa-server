#include "sa/bot/traveler.hpp"

#include <algorithm>
#include <cstdint>

namespace sa::bot {

Traveler::Traveler(std::uint64_t seed, TravelerParams params)
    : params_(params), roam_(seed, params.roam) {}

const SeenPortal* Traveler::NearestPortal(const Observation& observation) {
    const SeenPortal* best = nullptr;
    float best_sq = 0.0F;
    for (const SeenPortal& portal : observation.portals) {
        const float d_sq = common::DistanceSq(observation.self_position, portal.position);
        if (best == nullptr || d_sq < best_sq || (d_sq == best_sq && portal.id < best->id)) {
            best = &portal;
            best_sq = d_sq;
        }
    }
    return best;
}

Intent Traveler::Ask(const Observation& observation, const SeenPortal& portal) {
    asked_in_ = observation.zone;
    ask_again_at_ = common::TickIndex{
        observation.tick.value() +
        std::max<std::uint64_t>(1, TicksCovering(params_.request_retry, observation.tick_hz))};
    return Intent{.change_zone = portal.id};
}

Intent Traveler::Decide(const Observation& observation) {
    if (observation.tick_hz == 0) {
        return Intent{};  // not welcomed yet: no zone, no clock
    }
    if (zone_ != observation.zone) {
        // A new zone: the first Welcome, or the one a crossing brought. Each zone counts its own
        // ticks, so the timers start again from this zone's clock.
        if (asked_in_ && asked_in_ != observation.zone) {
            ++crossings_;
        }
        asked_in_.reset();
        zone_ = observation.zone;
        leave_after_ = common::TickIndex{observation.tick.value() +
                                         TicksCovering(params_.dwell, observation.tick_hz)};
    }
    if (observation.self_hp <= 0) {
        return Intent{};
    }
    const SeenPortal* portal = NearestPortal(observation);
    if (asked_in_) {
        // The zone holds the bot still while the Backend decides; ask again only if it stays.
        if (observation.tick < ask_again_at_ || portal == nullptr) {
            return Intent{};
        }
        return Ask(observation, *portal);
    }
    if (observation.tick < leave_after_ || portal == nullptr) {
        return roam_.Decide(observation);  // dwelling, or a zone without exits
    }
    if (!common::WithinDistance(observation.self_position, portal->position,
                                std::min(params_.portal_approach_m, portal->radius_m))) {
        return Intent{.direction = (portal->position - observation.self_position).Normalized()};
    }
    return Ask(observation, *portal);
}

}  // namespace sa::bot
