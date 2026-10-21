#include "local_bot_host.hpp"

#include "sa/bot/observation.hpp"

namespace sa::zone {

LocalBotHost::LocalBotHost(sim::Zone& zone, const DevBotConfig& config,
                           std::uint32_t ticks_per_snapshot, std::uint64_t seed)
    : zone_(zone), ticks_per_snapshot_(ticks_per_snapshot) {
    bots_.reserve(config.count);
    for (std::uint32_t i = 0; i < config.count; ++i) {
        // Dev bots are not real characters; ids are local to this process.
        const auto entity = zone_.SpawnPlayer(common::CharacterId{i + 1}, config.class_id);
        if (!entity) {
            continue;  // class missing from content: nothing to drive
        }
        bots_.push_back(Bot{.entity = *entity, .brain = bot::Wanderer{seed + i, config.wanderer}});
    }
}

void LocalBotHost::OnTick(common::TickIndex tick) {
    const common::Vec2 map_size = zone_.map().size;
    for (Bot& b : bots_) {
        const auto position = zone_.PositionOf(b.entity);
        if (!position) {
            continue;
        }
        const bot::Observation observation{.self_position = *position,
                                           .map_size = map_size,
                                           .self_hp = 1,
                                           .self_max_hp = 1,
                                           .visible = {},
                                           .tick = tick,
                                           .tick_hz = zone_.config().simulation_rate.hz};
        const common::Vec2 direction = b.brain.Decide(observation).direction;
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
