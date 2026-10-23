#include "sa/zone/sim/spatial_grid.hpp"

#include <algorithm>
#include <cmath>

namespace sa::zone::sim {
namespace {

// Index of the cell holding `raw` (a coordinate already divided by the cell size), clamped to
// the grid. NaN lands in cell 0 instead of reaching an undefined float-to-integer cast.
std::uint32_t ClampToCells(float raw, std::uint32_t cells) noexcept {
    if (std::isnan(raw) || raw <= 0.0F) {
        return 0;
    }
    return static_cast<std::uint32_t>(std::min(raw, static_cast<float>(cells - 1)));
}

}  // namespace

SpatialGrid::SpatialGrid(common::Vec2 map_size, float cell_size)
    : cell_size_(cell_size),
      columns_(std::max(1U, static_cast<std::uint32_t>(std::ceil(map_size.x / cell_size)))),
      rows_(std::max(1U, static_cast<std::uint32_t>(std::ceil(map_size.y / cell_size)))),
      cells_(static_cast<std::size_t>(columns_) * rows_) {}

std::uint32_t SpatialGrid::ColumnFor(float x) const noexcept {
    return ClampToCells(std::floor(x / cell_size_), columns_);
}

std::uint32_t SpatialGrid::RowFor(float y) const noexcept {
    return ClampToCells(std::floor(y / cell_size_), rows_);
}

SpatialGrid::CellIndex SpatialGrid::CellFor(common::Vec2 position) const noexcept {
    return (RowFor(position.y) * columns_) + ColumnFor(position.x);
}

void SpatialGrid::Insert(common::EntityId id, common::Vec2 position) {
    if (const auto it = slot_of_.find(id); it != slot_of_.end()) {
        Relocate(id, it->second, position);
        return;
    }
    Append(id, position);
}

void SpatialGrid::Append(common::EntityId id, common::Vec2 position) {
    const CellIndex cell = CellFor(position);
    slot_of_.emplace(id, Slot{.cell = cell, .index = cells_[cell].size()});
    cells_[cell].push_back(Entry{.id = id, .position = position});
}

void SpatialGrid::Remove(common::EntityId id) {
    const auto it = slot_of_.find(id);
    if (it == slot_of_.end()) {
        return;
    }
    const Slot slot = it->second;
    slot_of_.erase(it);
    Detach(slot);
}

void SpatialGrid::Detach(Slot slot) {
    std::vector<Entry>& bucket = cells_[slot.cell];
    if (slot.index + 1 != bucket.size()) {
        bucket[slot.index] = bucket.back();
        slot_of_.at(bucket[slot.index].id).index = slot.index;
    }
    bucket.pop_back();
}

void SpatialGrid::Move(common::EntityId id, common::Vec2 new_position) {
    if (const auto it = slot_of_.find(id); it != slot_of_.end()) {
        Relocate(id, it->second, new_position);
        return;
    }
    Append(id, new_position);
}

void SpatialGrid::Relocate(common::EntityId id, Slot& slot, common::Vec2 new_position) {
    const CellIndex target = CellFor(new_position);
    if (target == slot.cell) {
        cells_[target][slot.index].position = new_position;
        return;
    }
    const Slot old = slot;
    slot = Slot{.cell = target, .index = cells_[target].size()};
    cells_[target].push_back(Entry{.id = id, .position = new_position});
    Detach(old);
}

void SpatialGrid::QueryWithin(common::Vec2 center, float radius, std::vector<Entry>& out) const {
    const float radius_sq = radius * radius;
    const std::uint32_t c0 = ColumnFor(center.x - radius);
    const std::uint32_t c1 = ColumnFor(center.x + radius);
    const std::uint32_t r0 = RowFor(center.y - radius);
    const std::uint32_t r1 = RowFor(center.y + radius);
    for (std::uint32_t r = r0; r <= r1; ++r) {
        for (std::uint32_t c = c0; c <= c1; ++c) {
            for (const Entry& entry : cells_[(r * columns_) + c]) {
                if (common::DistanceSq(center, entry.position) <= radius_sq) {
                    out.push_back(entry);
                }
            }
        }
    }
}

}  // namespace sa::zone::sim
