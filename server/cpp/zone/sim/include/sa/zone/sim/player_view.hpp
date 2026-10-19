#pragma once

#include "sa/common/ids.hpp"
#include "sa/zone/sim/entities.hpp"

#include <cstdint>
#include <vector>

namespace sa::zone::sim {

// Delta for one player since the previous view was collected (see README "네트워크와 입장").
// `spawned` entities entered the interest set, `despawned` left it, `updated` stayed and changed
// (moved or took damage). `self` is always present: the client reconciles its prediction with it.
struct PlayerView {
    common::EntityId player;
    std::uint32_t last_processed_input_seq = 0;
    EntitySnapshot self;
    Vitals self_vitals;
    std::vector<EntitySnapshot> spawned;
    std::vector<EntitySnapshot> updated;
    std::vector<common::EntityId> despawned;

    [[nodiscard]] bool IsEmpty() const noexcept {
        return spawned.empty() && updated.empty() && despawned.empty();
    }
};

}  // namespace sa::zone::sim
