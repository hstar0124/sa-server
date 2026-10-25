#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sa::zone::sim {

// Uniform grid over the map (see README "Zone의 한 틱"). Answers "which entities are near this
// point" by scanning the cells a circle overlaps instead of every entity in the zone. Each cell
// keeps its entities' positions, so a query measures distances without looking anything up.
class SpatialGrid {
public:
    struct Entry {
        common::EntityId id;
        common::Vec2 position;
    };

    SpatialGrid(common::Vec2 map_size, float cell_size);

    void Insert(common::EntityId id, common::Vec2 position);
    void Remove(common::EntityId id);
    // Cheap when the entity stays in its cell, which is the common case per tick.
    void Move(common::EntityId id, common::Vec2 new_position);

    // Appends the entities within `radius` of `center`, with their positions, in no particular
    // order.
    void QueryWithin(common::Vec2 center, float radius, std::vector<Entry>& out) const;

    [[nodiscard]] std::size_t size() const noexcept { return slot_of_.size(); }
    [[nodiscard]] std::uint32_t columns() const noexcept { return columns_; }
    [[nodiscard]] std::uint32_t rows() const noexcept { return rows_; }

private:
    using CellIndex = std::uint32_t;
    struct Slot {
        CellIndex cell = 0;
        std::size_t index = 0;  // position in the cell's vector
    };

    [[nodiscard]] CellIndex CellFor(common::Vec2 position) const noexcept;
    [[nodiscard]] std::uint32_t ColumnFor(float x) const noexcept;
    [[nodiscard]] std::uint32_t RowFor(float y) const noexcept;
    void Append(common::EntityId id, common::Vec2 position);
    // Puts an entity already in the grid at a new position; `slot` is its entry in slot_of_.
    void Relocate(common::EntityId id, Slot& slot, common::Vec2 new_position);
    // Removes the entry at `slot` by moving the cell's last entry into its place.
    void Detach(Slot slot);

    float cell_size_;
    std::uint32_t columns_;
    std::uint32_t rows_;
    std::vector<std::vector<Entry>> cells_;
    std::unordered_map<common::EntityId, Slot> slot_of_;
};

}  // namespace sa::zone::sim
