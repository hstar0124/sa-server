#include "zone_host.hpp"

#include "codec.hpp"
#include "snapshot_encoder.hpp"
#include "trade_codec.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace sa::zone {

using internal::v1::ZoneToGateway;

ZoneHost::ZoneHost(sim::Zone& zone, Identity identity, EconomyGateway& economy,
                   DirectoryGateway& directory, TradeGateway& trades,
                   std::function<bool()> authoritative)
    : zone_(zone),
      identity_(identity),
      economy_(economy),
      directory_(directory),
      trades_(trades),
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
    // The session id is parsed here, once; everything inside the host uses the typed id.
    const auto parsed = common::InstanceId::FromString(attach.gateway_session_id());
    if (!parsed) {
        ++stats_.bad_inputs;  // a gateway bug: not a session id at all
        return;
    }
    const common::InstanceId& session_id = *parsed;
    if (!authoritative_()) {
        Reject(sink, session_id, internal::v1::REJECT_REASON_NOT_AUTHORITATIVE);
        return;
    }
    if (sessions_.contains(session_id)) {
        Reject(sink, session_id, internal::v1::REJECT_REASON_DUPLICATE_SESSION);
        return;
    }
    const common::CharacterId character{attach.character_id()};
    const common::OwnerEpoch epoch{attach.owner_epoch()};
    // A character is simulated once. The Backend raises the generation on every entry and
    // transfer, so the newer attach is the current one.
    if (const auto live = SessionOfCharacter(character)) {
        if (sessions_.at(*live).epoch >= epoch) {
            ++stats_.stale_attaches;  // the late echo of a session already superseded
            Reject(sink, session_id, internal::v1::REJECT_REASON_STALE_EPOCH);
            return;
        }
        // A newer grant for a character still here, e.g. a re-entry through another gateway
        // while the old gateway's link has not closed yet: the old session is over.
        ++stats_.superseded_sessions;
        Evict(*live, internal::v1::EVICT_REASON_NOT_OWNER);
    }
    if (sessions_.size() >= identity_.max_sessions) {
        Reject(sink, session_id, internal::v1::REJECT_REASON_ZONE_FULL);
        return;
    }

    // A checkpoint or an arrival point; the zone falls back to a spawn point if it is not
    // walkable here.
    const std::optional<common::Vec2> position =
        attach.has_position()
            ? std::optional{common::Vec2{attach.position().x(), attach.position().y()}}
            : std::nullopt;
    const auto spawned = zone_.SpawnPlayer(character, sim::ClassId{attach.class_id()}, position);
    if (!spawned) {
        spdlog::warn("attach refused: class {} is not in the content bundle", attach.class_id());
        Reject(sink, session_id, internal::v1::REJECT_REASON_UNKNOWN_CLASS);
        return;
    }
    const common::EntityId entity = *spawned;
    const Session session{.sink = &sink, .entity = entity, .character = character, .epoch = epoch};
    sessions_.emplace(session_id, session);
    session_by_entity_.emplace(entity, session_id);
    ++stats_.attaches;

    ZoneToGateway attached;
    auto* a = attached.mutable_session_attached();
    a->set_gateway_session_id(session_id.ToWire());
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
        .portals = zone_.map().portals,
    });
    SendToSession(session, session_id, welcome);
    RequestAssets(session_id);
    spdlog::debug("session attached character={} entity={} epoch={}", character.value(),
                  entity.value(), attach.owner_epoch());
}

void ZoneHost::Detach(const internal::v1::SessionDetach& detach) {
    const auto session_id = common::InstanceId::FromString(detach.gateway_session_id());
    const auto it = session_id ? sessions_.find(*session_id) : sessions_.end();
    if (it == sessions_.end()) {
        return;
    }
    const common::InstanceId id = it->first;
    SaveCheckpoint(id, it->second);  // where the character logged out
    RemoveSession(id, true);
    ++stats_.detaches;
}

void ZoneHost::Input(const internal::v1::SessionInput& input) {
    const auto session_id = common::InstanceId::FromString(input.gateway_session_id());
    const auto it = session_id ? sessions_.find(*session_id) : sessions_.end();
    if (it == sessions_.end()) {
        ++stats_.bad_inputs;
        return;
    }
    const game::v1::ClientMessage& message = input.message();
    const auto submit = [this](const auto& decoded) {
        if (!decoded) {
            ++stats_.bad_inputs;  // refused by the codec: never reaches the simulation
            return;
        }
        zone_.Submit(*decoded);
        ++stats_.inputs;
    };
    switch (message.body_case()) {
        case game::v1::ClientMessage::kMoveInput:
            submit(codec::ToMoveCommand(it->second.entity, message.move_input()));
            return;
        case game::v1::ClientMessage::kCastSkill:
            submit(codec::ToCastCommand(it->second.entity, message.cast_skill()));
            return;
        case game::v1::ClientMessage::kPickupItem:
            zone_.Submit(codec::ToPickupCommand(it->second.entity, message.pickup_item()));
            ++stats_.inputs;
            return;
        case game::v1::ClientMessage::kChangeZone:
            zone_.Submit(codec::ToChangeZoneCommand(it->second.entity, message.change_zone()));
            ++stats_.inputs;
            return;
        case game::v1::ClientMessage::kTradeOpen:
            submit(codec::ToTradeCommand(it->second.entity, message.trade_open()));
            return;
        case game::v1::ClientMessage::kTradeOffer:
            submit(codec::ToTradeCommand(it->second.entity, message.trade_offer()));
            return;
        case game::v1::ClientMessage::kTradeAccept:
            submit(codec::ToTradeCommand(it->second.entity, message.trade_accept()));
            return;
        case game::v1::ClientMessage::kTradeCancel:
            submit(codec::ToTradeCommand(it->second.entity, message.trade_cancel()));
            return;
        case game::v1::ClientMessage::kHello:
        case game::v1::ClientMessage::kPing:
        case game::v1::ClientMessage::BODY_NOT_SET:
            // Hello and Ping are consumed by the gateway. Any of them here is a client or gateway
            // bug and shows up in metrics.
            ++stats_.bad_inputs;
            return;
    }
}

void ZoneHost::LinkClosed(const LinkSink& sink) {
    std::vector<common::InstanceId> gone;
    for (const auto& [id, session] : sessions_) {
        if (session.sink == &sink) {
            gone.push_back(id);
        }
    }
    for (const common::InstanceId& id : gone) {
        SaveCheckpoint(id, sessions_.at(id));  // the ownership outlives the gateway's link
        RemoveSession(id, true);
        ++stats_.detaches;
    }
}

void ZoneHost::EvictAll(internal::v1::EvictReason reason) {
    std::vector<common::InstanceId> ids;
    ids.reserve(sessions_.size());
    for (const auto& [id, _] : sessions_) {
        ids.push_back(id);
    }
    for (const common::InstanceId& id : ids) {
        Evict(id, reason);
    }
}

void ZoneHost::Evict(const common::InstanceId& session_id, internal::v1::EvictReason reason) {
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return;
    }
    const Session session = it->second;
    ZoneToGateway evicted;
    evicted.mutable_session_evicted()->set_gateway_session_id(session_id.ToWire());
    evicted.mutable_session_evicted()->set_reason(reason);
    session.sink->Send(evicted);
    RemoveSession(session_id, true);
    ++stats_.evictions;
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

    // Kills, pickups, departures and trade steps the zone adjudicated this tick go to the Backend
    // now; their
    // answers come back on this thread and are applied through the zone's answer entry points.
    for (const sim::HostRequest& request : zone_.DrainHostRequests()) {
        std::visit(
            [this](const auto& r) {
                using T = std::decay_t<decltype(r)>;
                if constexpr (std::is_same_v<T, sim::KillRecord>) {
                    RecordKill(r);
                } else if constexpr (std::is_same_v<T, sim::PickupReservation>) {
                    SettlePickup(r);
                } else if constexpr (std::is_same_v<T, sim::TradeRequest>) {
                    Trade(r);
                } else {
                    Transfer(r);
                }
            },
            request);
    }

    if (tick.value() % identity_.ticks_per_checkpoint == 0) {
        SaveCheckpoints();
    }

    if (tick.value() % identity_.ticks_per_snapshot != 0) {
        return;
    }
    SendSnapshots(tick);
}

// Snapshots are state, not events: a view carries everything changed since that player's previous
// one, so an older snapshot is never worth more than the next. A gateway that falls behind
// therefore never makes the zone queue stale state for it. Each snapshot tick a link may grow to
// its byte budget; within it the sessions that have waited longest go first, and the rest keep
// their views uncollected until a later tick, losing nothing and catching up oldest first
// (ADR-0011). Each snapshot is encoded straight into the link's wire format (snapshot_encoder).
void ZoneHost::SendSnapshots(common::TickIndex tick) {
    by_link_.clear();
    for (auto& [id, session] : sessions_) {
        by_link_[session.sink].push_back(SessionSlot{.id = &id, .session = &session});
    }
    for (auto& [sink, slots] : by_link_) {
        std::ranges::sort(slots, [](const SessionSlot& a, const SessionSlot& b) {
            return a.session->last_snapshot < b.session->last_snapshot ||
                   (a.session->last_snapshot == b.session->last_snapshot &&
                    a.session->entity < b.session->entity);
        });
        std::size_t queued = sink->QueuedBytes();
        for (const SessionSlot& slot : slots) {
            if (queued >= identity_.snapshot_budget_bytes) {
                ++stats_.snapshots_deferred;  // the view stays uncollected: nothing is lost
                continue;
            }
            Session& session = *slot.session;
            EncodeSnapshotOutput(slot.id->ToWire(), zone_.CollectView(session.entity), tick,
                                 snapshot_scratch_);
            queued += snapshot_scratch_.size();
            sink->SendPayload(std::as_bytes(std::span{snapshot_scratch_}));
            session.last_snapshot = tick;
            ++stats_.snapshots_sent;
            stats_.snapshot_bytes += snapshot_scratch_.size();
        }
        link_queue_peak_ = std::max(link_queue_peak_, sink->QueuedBytes());
    }
}

ZoneHost::LinkPressure ZoneHost::TakeLinkPressure() {
    return LinkPressure{.queue_peak_bytes = std::exchange(link_queue_peak_, 0)};
}

// ---- helpers -----------------------------------------------------------------------------------

codec::ZoneAuthority ZoneHost::AuthorityFor(const Session& session) const {
    return codec::ZoneAuthority{.zone_instance = identity_.zone_instance, .epoch = session.epoch};
}

ZoneHost::Session* ZoneHost::SessionOf(common::EntityId entity) {
    const auto by_entity = session_by_entity_.find(entity);
    if (by_entity == session_by_entity_.end()) {
        return nullptr;
    }
    const auto session = sessions_.find(by_entity->second);
    return session == sessions_.end() ? nullptr : &session->second;
}

void ZoneHost::Reject(LinkSink& sink, const common::InstanceId& session_id,
                      internal::v1::RejectReason reason) {
    ZoneToGateway rejected;
    rejected.mutable_session_rejected()->set_gateway_session_id(session_id.ToWire());
    rejected.mutable_session_rejected()->set_reason(reason);
    sink.Send(rejected);
    ++stats_.rejects;
}

void ZoneHost::SendToSession(const Session& session, const common::InstanceId& session_id,
                             const game::v1::ServerMessage& message) {
    ZoneToGateway out;
    auto* output = out.mutable_session_output();
    output->set_gateway_session_id(session_id.ToWire());
    *output->mutable_message() = message;
    session.sink->Send(out);
}

void ZoneHost::RemoveSession(const common::InstanceId& session_id, bool despawn) {
    const auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return;
    }
    if (it->second.trading) {
        CancelTradesOf(it->second);  // whatever ended the session: detach, kick, link, eviction
    }
    if (despawn) {
        zone_.DespawnPlayer(it->second.entity);
    }
    session_by_entity_.erase(it->second.entity);
    sessions_.erase(it);
}

}  // namespace sa::zone
