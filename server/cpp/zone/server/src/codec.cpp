#include "codec.hpp"

#include <variant>

namespace sa::zone::codec {
namespace {

std::uint32_t Clamped(std::int32_t value) {
    return static_cast<std::uint32_t>(std::max(0, value));
}

void Fill(game::v1::Vec2& out, common::Vec2 v) {
    out.set_x(v.x);
    out.set_y(v.y);
}

void Fill(game::v1::EntityUpdate& out, const sim::EntitySnapshot& s) {
    out.set_entity_id(s.id.value());
    Fill(*out.mutable_position(), s.position);
    Fill(*out.mutable_velocity(), s.velocity);
    out.set_hp(Clamped(s.hp));
}

void Fill(game::v1::EntitySpawn& out, const sim::EntitySnapshot& s) {
    out.set_entity_id(s.id.value());
    out.set_kind(ToProto(s.kind));
    out.set_template_id(s.template_id);
    Fill(*out.mutable_position(), s.position);
    out.set_hp(Clamped(s.hp));
    out.set_max_hp(Clamped(s.max_hp));
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

game::v1::RejectCode ToProto(sim::RejectReason reason) {
    switch (reason) {
        case sim::RejectReason::kNone:
            return game::v1::REJECT_CODE_UNSPECIFIED;
        case sim::RejectReason::kUnknownSkill:
            return game::v1::REJECT_CODE_UNKNOWN_SKILL;
        case sim::RejectReason::kNotLearned:
            return game::v1::REJECT_CODE_NOT_LEARNED;
        case sim::RejectReason::kDead:
            return game::v1::REJECT_CODE_DEAD;
        case sim::RejectReason::kAlreadyCasting:
            return game::v1::REJECT_CODE_ALREADY_CASTING;
        case sim::RejectReason::kNotEnoughMp:
            return game::v1::REJECT_CODE_NOT_ENOUGH_MP;
        case sim::RejectReason::kOnCooldown:
            return game::v1::REJECT_CODE_ON_COOLDOWN;
        case sim::RejectReason::kInvalidTarget:
            return game::v1::REJECT_CODE_INVALID_TARGET;
        case sim::RejectReason::kOutOfRange:
            return game::v1::REJECT_CODE_OUT_OF_RANGE;
    }
    return game::v1::REJECT_CODE_UNSPECIFIED;
}

sim::MoveCommand ToMoveCommand(common::EntityId player, const game::v1::MoveInput& input) {
    return sim::MoveCommand{.player = player,
                            .input_seq = input.input_seq(),
                            .direction = {input.direction().x(), input.direction().y()}};
}

sim::CastCommand ToCastCommand(common::EntityId player, const game::v1::CastSkill& cast) {
    sim::SkillTarget target = common::EntityId{};  // an absent target is rejected by the zone
    if (cast.has_target_entity_id()) {
        target = common::EntityId{cast.target_entity_id()};
    } else if (cast.has_direction()) {
        target = common::Vec2{cast.direction().x(), cast.direction().y()};
    }
    return sim::CastCommand{.player = player,
                            .request_id = common::RequestId{cast.request_id()},
                            .skill = sim::SkillId{cast.skill_id()},
                            .target = target};
}

game::v1::WorldSnapshot ToWorldSnapshot(const sim::PlayerView& view, common::TickIndex tick) {
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
    Fill(*out.mutable_self(), view.self);
    auto* vitals = out.mutable_self_vitals();
    vitals->set_hp(Clamped(view.self_vitals.hp));
    vitals->set_max_hp(Clamped(view.self_vitals.max_hp));
    vitals->set_mp(Clamped(view.self_vitals.mp));
    vitals->set_max_mp(Clamped(view.self_vitals.max_mp));
    return out;
}

game::v1::CommandResult ToCommandResult(const sim::CommandResultEvent& event) {
    game::v1::CommandResult out;
    out.set_request_id(event.request_id.value());
    out.set_status(event.accepted() ? game::v1::COMMAND_STATUS_ACCEPTED
                                    : game::v1::COMMAND_STATUS_REJECTED);
    out.set_reject_code(ToProto(event.reason));
    return out;
}

game::v1::CombatEvent ToCombatEvent(const sim::CombatEvent& event) {
    game::v1::CombatEvent out;
    out.set_attacker_id(event.attacker.value());
    out.set_target_id(event.target.value());
    out.set_skill_id(event.skill.value());
    out.set_damage(Clamped(event.damage));
    out.set_target_hp(Clamped(event.target_hp));
    out.set_killed(event.killed);
    return out;
}

game::v1::ServerMessage ToServerMessage(const sim::ZoneEvent& event) {
    game::v1::ServerMessage out;
    std::visit(
        [&out](const auto& e) {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, sim::CommandResultEvent>) {
                *out.mutable_command_result() = ToCommandResult(e);
            } else {
                *out.mutable_combat_event() = ToCombatEvent(e);
            }
        },
        event);
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
