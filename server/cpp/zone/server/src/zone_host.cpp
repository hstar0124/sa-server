#include "zone_host.hpp"

#include "codec.hpp"

#include <spdlog/spdlog.h>

#include <type_traits>
#include <vector>

namespace sa::zone {

using internal::v1::ZoneToGateway;

ZoneHost::ZoneHost(sim::Zone& zone, Identity identity, EconomyGateway& economy,
                   std::function<bool()> authoritative)
    : zone_(zone),
      identity_(identity),
      economy_(economy),
      authoritative_(std::move(authoritative)) {}

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
    const auto spawned = zone_.SpawnPlayer(character, sim::ClassId{attach.class_id()});
    if (!spawned) {
        spdlog::warn("attach refused: class {} is not in the content bundle", attach.class_id());
        Reject(sink, session_id, internal::v1::REJECT_REASON_UNKNOWN_CLASS);
        return;
    }
    const common::EntityId entity = *spawned;
    const Session session{.sink = &sink,
                          .entity = entity,
                          .character = character,
                          .epoch = common::OwnerEpoch{attach.owner_epoch()}};
    sessions_.emplace(session_id, session);
    session_by_entity_.emplace(entity, session_id);
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
    RequestAssets(session_id);
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
            zone_.Submit(codec::ToCastCommand(it->second.entity, message.cast_skill()));
            ++stats_.inputs;
            return;
        case game::v1::ClientMessage::kPickupItem:
            zone_.Submit(codec::ToPickupCommand(it->second.entity, message.pickup_item()));
            ++stats_.inputs;
            return;
        case game::v1::ClientMessage::kChangeZone:
        case game::v1::ClientMessage::kHello:
        case game::v1::ClientMessage::kPing:
        case game::v1::ClientMessage::BODY_NOT_SET:
            // Transfers arrive in stage 8; Hello and Ping are consumed by the gateway. Any of
            // them here is a client or gateway bug and shows up in metrics.
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
    // Command results and combat notifications go out every tick: a client waiting on a cast
    // result should not also wait for the next snapshot.
    for (const sim::DeliveredEvent& delivered : zone_.DrainEvents()) {
        const auto by_entity = session_by_entity_.find(delivered.recipient);
        if (by_entity == session_by_entity_.end()) {
            continue;  // recipient left this tick
        }
        const auto session = sessions_.find(by_entity->second);
        if (session == sessions_.end()) {
            continue;
        }
        SendToSession(session->second, by_entity->second, codec::ToServerMessage(delivered.event));
        ++stats_.events_sent;
    }

    // Kills and pickups the zone adjudicated this tick go to the Backend now; their answers come
    // back on this thread and are applied through the zone's answer entry points.
    for (const sim::HostRequest& request : zone_.DrainHostRequests()) {
        std::visit(
            [this](const auto& r) {
                if constexpr (std::is_same_v<std::decay_t<decltype(r)>, sim::KillRecord>) {
                    RecordKill(r);
                } else {
                    SettlePickup(r);
                }
            },
            request);
    }

    if (tick.value() % identity_.ticks_per_snapshot != 0) {
        return;
    }
    for (const auto& [id, session] : sessions_) {
        const sim::PlayerView view = zone_.CollectView(session.entity);
        game::v1::ServerMessage message;
        *message.mutable_world_snapshot() = codec::ToWorldSnapshot(view, tick);
        SendToSession(session, id, message);
        ++stats_.snapshots_sent;
    }
}

// ---- economy -----------------------------------------------------------------------------------

codec::ZoneAuthority ZoneHost::AuthorityFor(const Session& session) const {
    return codec::ZoneAuthority{.zone_instance = identity_.zone_instance, .epoch = session.epoch};
}

void ZoneHost::RequestAssets(const std::string& session_id) {
    const Session& session = sessions_.at(session_id);
    backend::v1::GetCharacterAssetsRequest request;
    request.set_world_id(zone_.config().world_id.value());
    request.set_character_id(session.character.value());
    economy_.GetCharacterAssets(
        request, [this, session_id, entity = session.entity](
                     const std::optional<backend::v1::GetCharacterAssetsResponse>& response) {
            OnAssets(session_id, entity, response);
        });
}

void ZoneHost::OnAssets(const std::string& session_id, common::EntityId entity,
                        const std::optional<backend::v1::GetCharacterAssetsResponse>& response) {
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end() || it->second.entity != entity) {
        return;  // the session ended while the Backend was answering
    }
    if (!response) {
        ++stats_.backend_failures;
        spdlog::warn("assets of character {} unavailable; the client keeps an empty inventory",
                     it->second.character.value());
        return;
    }
    it->second.assets = codec::ToAssets(*response, zone_.rules().economy.primary_currency);
    SendAssets(session_id, it->second);
}

void ZoneHost::RecordKill(const sim::KillRecord& kill) {
    const Session* session = SessionOf(kill.killer);
    if (session == nullptr) {
        return;  // the killer left before the kill was recorded; the grant cannot be fenced
    }
    const common::KillId kill_id = common::KillId::Generate();
    economy_.RecordKill(codec::ToRecordKillRequest(kill, kill_id, zone_.config().world_id,
                                                   AuthorityFor(*session), zone_.content().version),
                        [this, killer = kill.killer,
                         kill_id](const std::optional<backend::v1::RecordKillResponse>& response) {
                            OnKillRecorded(killer, kill_id, response);
                        });
}

void ZoneHost::OnKillRecorded(common::EntityId killer, common::KillId kill_id,
                              const std::optional<backend::v1::RecordKillResponse>& response) {
    if (!response) {
        ++stats_.backend_failures;
        spdlog::warn("kill {} was not recorded; the reward is lost", kill_id.ToHex());
        return;
    }
    switch (response->outcome()) {
        case backend::v1::KILL_OUTCOME_RECORDED:
        case backend::v1::KILL_OUTCOME_ALREADY_RECORDED: {
            const std::vector<sim::GroundItem> items = codec::ToGroundItems(*response);
            const auto spawned = zone_.SpawnGroundItems(items);
            if (spawned.size() != items.size()) {
                spdlog::warn("kill {}: {} of {} items name templates this zone does not know",
                             kill_id.ToHex(), items.size() - spawned.size(), items.size());
            }
            ++stats_.kills_recorded;
            break;
        }
        case backend::v1::KILL_OUTCOME_STALE_AUTHORITY:
        case backend::v1::KILL_OUTCOME_CONTENT_VERSION_MISMATCH:
        case backend::v1::KILL_OUTCOME_UNKNOWN_TEMPLATE:
        case backend::v1::KILL_OUTCOME_UNSPECIFIED:
        default:
            ++stats_.kills_refused;
            spdlog::warn("kill {} refused: {}", kill_id.ToHex(),
                         backend::v1::KillOutcome_Name(response->outcome()));
            return;
    }
    const auto by_entity = session_by_entity_.find(killer);
    if (by_entity == session_by_entity_.end()) {
        return;
    }
    Session& session = sessions_.at(by_entity->second);
    if (session.assets) {
        session.assets->gold = response->gold_balance();
        SendAssets(by_entity->second, session);
    }
}

void ZoneHost::SettlePickup(const sim::PickupReservation& pickup) {
    const Session* session = SessionOf(pickup.player);
    if (session == nullptr) {
        zone_.ResolvePickup(pickup.item, sim::PickupOutcome::kUnavailable);
        return;
    }
    economy_.PickupItem(
        codec::ToPickupItemRequest(pickup, zone_.config().world_id, AuthorityFor(*session)),
        [this, pickup](const std::optional<backend::v1::PickupItemResponse>& response) {
            OnPickupSettled(pickup, response);
        });
}

void ZoneHost::OnPickupSettled(const sim::PickupReservation& pickup,
                               const std::optional<backend::v1::PickupItemResponse>& response) {
    if (!response) {
        ++stats_.backend_failures;
        zone_.ResolvePickup(pickup.item, sim::PickupOutcome::kUnavailable);
        return;
    }
    const sim::PickupOutcome outcome = codec::ToPickupOutcome(response->outcome());
    zone_.ResolvePickup(pickup.item, outcome);
    if (outcome != sim::PickupOutcome::kAcquired) {
        ++stats_.pickups_refused;
        if (outcome == sim::PickupOutcome::kUnavailable) {
            spdlog::warn("pickup by character {} not settled: {}", pickup.character.value(),
                         backend::v1::PickupOutcome_Name(response->outcome()));
        }
        return;
    }
    ++stats_.pickups_acquired;
    const auto by_entity = session_by_entity_.find(pickup.player);
    if (by_entity == session_by_entity_.end()) {
        return;
    }
    Session& session = sessions_.at(by_entity->second);
    if (!session.assets) {
        return;  // the initial inventory never arrived; nothing consistent to extend
    }
    if (const auto entry = codec::ToInventoryEntry(response->item())) {
        session.assets->items.push_back(*entry);
    }
    session.assets->version = response->inventory_version();
    SendAssets(by_entity->second, session);
}

// ---- helpers -----------------------------------------------------------------------------------

ZoneHost::Session* ZoneHost::SessionOf(common::EntityId entity) {
    const auto by_entity = session_by_entity_.find(entity);
    if (by_entity == session_by_entity_.end()) {
        return nullptr;
    }
    const auto session = sessions_.find(by_entity->second);
    return session == sessions_.end() ? nullptr : &session->second;
}

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

void ZoneHost::SendAssets(const std::string& session_id, Session& session) {
    if (!session.assets) {
        return;
    }
    game::v1::ServerMessage message;
    *message.mutable_inventory_update() = codec::ToInventoryUpdate(*session.assets);
    SendToSession(session, session_id, message);
    ++stats_.inventory_updates_sent;
}

void ZoneHost::RemoveSession(const std::string& session_id, bool despawn) {
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return;
    }
    if (despawn) {
        zone_.DespawnPlayer(it->second.entity);
    }
    session_by_entity_.erase(it->second.entity);
    sessions_.erase(it);
}

}  // namespace sa::zone
