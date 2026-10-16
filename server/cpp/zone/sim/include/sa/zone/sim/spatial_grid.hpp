#pragma once

#include "sa/common/geometry.hpp"
#include "sa/common/ids.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace sa::zone::sim {

// Uniform grid over the map (see README "Zone의 한 틱"). Answers "which entities are near this
// point" by scanning the cells a circle overlaps instead of every entity in the zone.
class SpatialGrid {
public:
    SpatialGrid(common::Vec2 map_size, float cell_size);

    void Insert(common::EntityId id, common::Vec2 position);
    void Remove(common::EntityId id);
    // Cheap when the entity stays in its cell, which is the common case per tick.
    void Move(common::EntityId id, common::Vec2 new_position);

    // Appends ids of entities whose cell overlaps the circle. Callers filter by exact distance.
    void QueryCandidates(common::Vec2 center, float radius,
                         std::vector<common::EntityId>& out) const;

    [[nodiscard]] std::size_t size() const noexcept { return cell_of_.size(); }
    [[nodiscard]] std::uint32_t columns() const noexcept { return columns_; }
    [[nodiscard]] std::uint32_t rows() const noexcept { return rows_; }

private:
    using CellIndex = std::uint32_t;

    [[nodiscard]] CellIndex CellFor(common::Vec2 position) const noexcept;
    [[nodiscard]] std::uint32_t ColumnFor(float x) const noexcept;
    [[nodiscard]] std::uint32_t RowFor(float y) const noexcept;

    float cell_size_;
    std::uint32_t columns_;
    std::uint32_t rows_;
    std::vector<std::vector<common::EntityId>> cells_;
    std::unordered_map<common::EntityId, CellIndex> cell_of_;
};

}  // namespace sa::zone::sim
