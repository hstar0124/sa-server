#include "local_bot_host.hpp"

#include "sa/bot/observation.hpp"

namespace sa::zone {
namespace {

// Dev bots dawdle a little at each destination so the crowd does not move in lockstep.
constexpr bot::WandererParams kDevBotParams{
    .arrive_distance_m = 1.0F,
    .retarget_after_ticks = 400,
    .idle_ticks_min = 0,
    .idle_ticks_max = 40,
    .margin_m = 2.0F,
};
constexpr std::uint32_t kDevBotClassId = 1;

}  // namespace

LocalBotHost::LocalBotHost(sim::Zone& zone, std::uint32_t count, std::uint32_t ticks_per_snapshot,
                           std::uint64_t seed)
    : zone_(zone), ticks_per_snapshot_(ticks_per_snapshot) {
    bots_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        // Dev bots are not real characters; ids are local to this process.
        const common::EntityId entity =
            zone_.SpawnPlayer(common::CharacterId{i + 1}, kDevBotClassId);
        bots_.push_back(Bot{.entity = entity, .brain = bot::Wanderer{seed + i, kDevBotParams}});
    }
}

void LocalBotHost::OnTick(common::TickIndex tick) {
    const common::Vec2 map_size = zone_.map().size;
    for (Bot& b : bots_) {
        const auto position = zone_.PositionOf(b.entity);
        if (!position) {
            continue;
        }
        const bot::Observation observation{
            .self_position = *position, .map_size = map_size, .visible = {}, .tick = tick.value()};
        const common::Vec2 direction = b.brain.Decide(observation);
        zone_.Submit(sim::MoveCommand{
            .player = b.entity, .input_seq = b.next_input_seq++, .direction = direction});
    }

    if (tick.value() % ticks_per_snapshot_ != 0) {
        return;
    }
    for (const Bot& b : bots_) {
        const sim::PlayerView view = zone_.CollectView(b.entity);
        ++totals_.views;
        totals_.spawned += view.spawned.size();
        totals_.updated += view.updated.size();
        totals_.despawned += view.despawned.size();
    }
}

}  // namespace sa::zone
