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
    const CellIndex cell = CellFor(position);
    cells_[cell].push_back(id);
    cell_of_.insert_or_assign(id, cell);
}

void SpatialGrid::Remove(common::EntityId id) {
    const auto it = cell_of_.find(id);
    if (it == cell_of_.end()) {
        return;
    }
    std::vector<common::EntityId>& bucket = cells_[it->second];
    std::erase(bucket, id);
    cell_of_.erase(it);
}

void SpatialGrid::Move(common::EntityId id, common::Vec2 new_position) {
    const auto it = cell_of_.find(id);
    if (it == cell_of_.end()) {
        Insert(id, new_position);
        return;
    }
    const CellIndex target = CellFor(new_position);
    if (target == it->second) {
        return;
    }
    std::erase(cells_[it->second], id);
    cells_[target].push_back(id);
    it->second = target;
}

void SpatialGrid::QueryCandidates(common::Vec2 center, float radius,
                                  std::vector<common::EntityId>& out) const {
    const std::uint32_t c0 = ColumnFor(center.x - radius);
    const std::uint32_t c1 = ColumnFor(center.x + radius);
    const std::uint32_t r0 = RowFor(center.y - radius);
    const std::uint32_t r1 = RowFor(center.y + radius);
    for (std::uint32_t r = r0; r <= r1; ++r) {
        for (std::uint32_t c = c0; c <= c1; ++c) {
            const std::vector<common::EntityId>& bucket = cells_[(r * columns_) + c];
            out.insert(out.end(), bucket.begin(), bucket.end());
        }
    }
}

}  // namespace sa::zone::sim
