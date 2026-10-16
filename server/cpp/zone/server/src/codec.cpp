#include "codec.hpp"

namespace sa::zone::codec {
namespace {

void Fill(game::v1::Vec2& out, common::Vec2 v) {
    out.set_x(v.x);
    out.set_y(v.y);
}

void Fill(game::v1::EntityUpdate& out, const sim::EntitySnapshot& s) {
    out.set_entity_id(s.id.value());
    Fill(*out.mutable_position(), s.position);
    Fill(*out.mutable_velocity(), s.velocity);
}

void Fill(game::v1::EntitySpawn& out, const sim::EntitySnapshot& s) {
    out.set_entity_id(s.id.value());
    out.set_kind(ToProto(s.kind));
    out.set_template_id(s.template_id);
    Fill(*out.mutable_position(), s.position);
}

}  // namespace

game::v1::EntityKind ToProto(sim::EntityKind kind) {
    switch (kind) {
        case sim::EntityKind::kPlayer:
            return game::v1::ENTITY_KIND_PLAYER;
        case sim::EntityKind::kMonster:
            return game::v1::ENTITY_KIND_MONSTER;
        case sim::EntityKind::kDroppedItem:
            return game::v1::ENTITY_KIND_DROPPED_ITEM;
        case sim::EntityKind::kProjectile:
            return game::v1::ENTITY_KIND_PROJECTILE;
    }
    return game::v1::ENTITY_KIND_UNSPECIFIED;
}

sim::MoveCommand ToMoveCommand(common::EntityId player, const game::v1::MoveInput& input) {
    return sim::MoveCommand{.player = player,
                            .input_seq = input.input_seq(),
                            .direction = {input.direction().x(), input.direction().y()}};
}

game::v1::WorldSnapshot ToWorldSnapshot(const sim::PlayerView& view,
                                        const sim::EntitySnapshot& self, common::TickIndex tick) {
    game::v1::WorldSnapshot out;
    out.set_server_tick(tick.value());
    out.set_last_processed_input_seq(view.last_processed_input_seq);
    for (const sim::EntitySnapshot& s : view.spawned) {
        Fill(*out.add_spawned(), s);
    }
    for (const sim::EntitySnapshot& s : view.updated) {
        Fill(*out.add_updated(), s);
    }
    for (const common::EntityId id : view.despawned) {
        out.add_despawned(id.value());
    }
    Fill(*out.mutable_self(), self);
    return out;
}

game::v1::Welcome ToWelcome(const WelcomeInfo& info) {
    game::v1::Welcome out;
    out.set_character_id(info.character.value());
    out.set_zone_id(info.zone.value());
    out.set_simulation_hz(info.simulation_hz);
    out.set_server_tick(info.tick.value());
    out.set_entity_id(info.entity.value());
    Fill(*out.mutable_map_size(), info.map_size);
    return out;
}

}  // namespace sa::zone::codec
