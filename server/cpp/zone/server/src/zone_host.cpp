#include "zone_host.hpp"

#include "codec.hpp"

#include <spdlog/spdlog.h>

#include <vector>

namespace sa::zone {

using internal::v1::ZoneToGateway;

ZoneHost::ZoneHost(sim::Zone& zone, Identity identity, std::function<bool()> authoritative)
    : zone_(zone), identity_(identity), authoritative_(std::move(authoritative)) {}

// ---- GatewayLink::Handler -------------------------------------------------------------------

void ZoneHost::OnLinkReady(GatewayLink& link) {
    ZoneToGateway welcome;
    auto* w = welcome.mutable_link_welcome();
    w->set_world_id(zone_.config().world_id.value());
    w->set_zone_id(zone_.config().zone_id.value());
    w->set_zone_instance_id(identity_.zone_instance.ToWire());
    w->set_simulation_hz(identity_.simulation_hz);
    link.Send(welcome);
    spdlog::info("gateway {} linked", link.gateway_id().value_or(common::InstanceId{}).ToHex());
}

void ZoneHost::OnAttach(GatewayLink& link, const internal::v1::SessionAttach& attach) {
    Attach(link, attach);
}
void ZoneHost::OnDetach(GatewayLink& /*link*/, const internal::v1::SessionDetach& detach) {
    Detach(detach);
}
void ZoneHost::OnInput(GatewayLink& /*link*/, const internal::v1::SessionInput& input) {
    Input(input);
}
void ZoneHost::OnLinkClosed(GatewayLink& link) {
    LinkClosed(link);
    if (on_link_closed_) {
        on_link_closed_(link);
    }
}

// ---- sessions ---------------------------------------------------------------------------------

void ZoneHost::Attach(LinkSink& sink, const internal::v1::SessionAttach& attach) {
    const std::string& session_id = attach.gateway_session_id();
    if (!authoritative_()) {
        Reject(sink, session_id, internal::v1::REJECT_REASON_NOT_AUTHORITATIVE);
        return;
    }
    if (sessions_.contains(session_id)) {
        Reject(sink, session_id, internal::v1::REJECT_REASON_DUPLICATE_SESSION);
        return;
    }
    if (sessions_.size() >= identity_.max_sessions) {
        Reject(sink, session_id, internal::v1::REJECT_REASON_ZONE_FULL);
        return;
    }

    const common::CharacterId character{attach.character_id()};
    const common::EntityId entity = zone_.SpawnPlayer(character, attach.class_id());
    const Session session{.sink = &sink,
                          .entity = entity,
                          .character = character,
                          .epoch = common::OwnerEpoch{attach.owner_epoch()}};
    sessions_.emplace(session_id, session);
    ++stats_.attaches;

    ZoneToGateway attached;
    auto* a = attached.mutable_session_attached();
    a->set_gateway_session_id(session_id);
    a->set_entity_id(entity.value());
    a->set_server_tick(zone_.current_tick().value());
    sink.Send(attached);

    game::v1::ServerMessage welcome;
    *welcome.mutable_welcome() = codec::ToWelcome(codec::WelcomeInfo{
        .character = character,
        .zone = zone_.config().zone_id,
        .simulation_hz = identity_.simulation_hz,
        .tick = zone_.current_tick(),
        .entity = entity,
        .map_size = zone_.map().size,
    });
    SendToSession(session, session_id, welcome);
    spdlog::debug("session attached character={} entity={} epoch={}", character.value(),
                  entity.value(), attach.owner_epoch());
}

void ZoneHost::Detach(const internal::v1::SessionDetach& detach) {
    if (sessions_.contains(detach.gateway_session_id())) {
        RemoveSession(detach.gateway_session_id(), true);
        ++stats_.detaches;
    }
}

void ZoneHost::Input(const internal::v1::SessionInput& input) {
    const auto it = sessions_.find(input.gateway_session_id());
    if (it == sessions_.end()) {
        ++stats_.bad_inputs;
        return;
    }
    const game::v1::ClientMessage& message = input.message();
    switch (message.body_case()) {
        case game::v1::ClientMessage::kMoveInput:
            zone_.Submit(codec::ToMoveCommand(it->second.entity, message.move_input()));
            ++stats_.inputs;
            return;
        case game::v1::ClientMessage::kCastSkill:
        case game::v1::ClientMessage::kPickupItem:
        case game::v1::ClientMessage::kChangeZone:
        case game::v1::ClientMessage::kHello:
        case game::v1::ClientMessage::kPing:
        case game::v1::ClientMessage::BODY_NOT_SET:
            // Skills, pickups and transfers arrive in stages 4-8; Hello and Ping are consumed by
            // the gateway. Any of them here is a client or gateway bug and shows up in metrics.
            ++stats_.bad_inputs;
            return;
    }
}

void ZoneHost::LinkClosed(const LinkSink& sink) {
    std::vector<std::string> gone;
    for (const auto& [id, session] : sessions_) {
        if (session.sink == &sink) {
            gone.push_back(id);
        }
    }
    for (const std::string& id : gone) {
        RemoveSession(id, true);
        ++stats_.detaches;
    }
}

void ZoneHost::EvictAll(internal::v1::EvictReason reason) {
    std::vector<std::string> ids;
    ids.reserve(sessions_.size());
    for (const auto& [id, _] : sessions_) {
        ids.push_back(id);
    }
    for (const std::string& id : ids) {
        const Session session = sessions_.at(id);
        ZoneToGateway evicted;
        evicted.mutable_session_evicted()->set_gateway_session_id(id);
        evicted.mutable_session_evicted()->set_reason(reason);
        session.sink->Send(evicted);
        RemoveSession(id, true);
        ++stats_.evictions;
    }
}

// ---- output ------------------------------------------------------------------------------------

void ZoneHost::OnTick(common::TickIndex tick) {
    if (tick.value() % identity_.ticks_per_snapshot != 0) {
        return;
    }
    for (const auto& [id, session] : sessions_) {
        const sim::PlayerView view = zone_.CollectView(session.entity);
        const auto self = zone_.SnapshotOf(session.entity);
        if (!self) {
            continue;
        }
        game::v1::ServerMessage message;
        *message.mutable_world_snapshot() = codec::ToWorldSnapshot(view, *self, tick);
        SendToSession(session, id, message);
        ++stats_.snapshots_sent;
    }
}

// ---- helpers -----------------------------------------------------------------------------------

void ZoneHost::Reject(LinkSink& sink, const std::string& session_id,
                      internal::v1::RejectReason reason) {
    ZoneToGateway rejected;
    rejected.mutable_session_rejected()->set_gateway_session_id(session_id);
    rejected.mutable_session_rejected()->set_reason(reason);
    sink.Send(rejected);
    ++stats_.rejects;
}

void ZoneHost::SendToSession(const Session& session, const std::string& session_id,
                             const game::v1::ServerMessage& message) {
    ZoneToGateway out;
    auto* output = out.mutable_session_output();
    output->set_gateway_session_id(session_id);
    *output->mutable_message() = message;
    session.sink->Send(out);
}

void ZoneHost::RemoveSession(const std::string& session_id, bool despawn) {
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return;
    }
    if (despawn) {
        zone_.DespawnPlayer(it->second.entity);
    }
    sessions_.erase(it);
}

}  // namespace sa::zone
