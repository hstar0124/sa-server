#pragma once

#include "sa/common/ids.hpp"
#include "sa/common/instance_id.hpp"
#include "sa/zone/sim/zone.hpp"

#include "codec.hpp"
#include "economy_client.hpp"
#include "gateway_link.hpp"
#include "link_sink.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace sa::zone {

struct ZoneHostStats {
    std::uint64_t attaches = 0;
    std::uint64_t rejects = 0;
    std::uint64_t detaches = 0;
    std::uint64_t evictions = 0;
    std::uint64_t inputs = 0;
    std::uint64_t snapshots_sent = 0;
    std::uint64_t events_sent = 0;
    std::uint64_t bad_inputs = 0;
    // Economy round trips (see README "자산과 멱등성").
    std::uint64_t kills_recorded = 0;
    std::uint64_t kills_refused = 0;  // stale authority, content mismatch, unknown template
    std::uint64_t pickups_acquired = 0;
    std::uint64_t pickups_refused = 0;
    std::uint64_t backend_failures = 0;  // no answer at all
    std::uint64_t inventory_updates_sent = 0;
};

// Binds gateway sessions to simulated players and settles the zone's economy requests with the
// Backend. Owns the session table and is the only code that translates between link messages,
// Economy RPCs and Zone calls. Single-threaded with the zone: Backend replies arrive on the same
// thread through the gateway's executor.
class ZoneHost final : public GatewayLink::Handler {
public:
    struct Identity {
        common::InstanceId zone_instance;
        std::uint32_t simulation_hz = 0;
        std::uint32_t ticks_per_snapshot = 0;
        std::uint32_t max_sessions = 0;
    };

    // `authoritative` answers whether the zone currently holds its Backend lease.
    ZoneHost(sim::Zone& zone, Identity identity, EconomyGateway& economy,
             std::function<bool()> authoritative);

    // Called after each simulation tick: delivers command results and combat events every tick,
    // forwards kills and pickups to the Backend, snapshots at the configured cadence.
    void OnTick(common::TickIndex tick);

    // Lease lost or shutdown: every session is told to leave and the players despawn.
    void EvictAll(internal::v1::EvictReason reason);

    // GatewayLink::Handler
    void OnLinkReady(GatewayLink& link) override;
    void OnAttach(GatewayLink& link, const internal::v1::SessionAttach& attach) override;
    void OnDetach(GatewayLink& link, const internal::v1::SessionDetach& detach) override;
    void OnInput(GatewayLink& link, const internal::v1::SessionInput& input) override;
    void OnLinkClosed(GatewayLink& link) override;

    // Link-agnostic entry points so the logic is testable with a fake sink.
    void Attach(LinkSink& sink, const internal::v1::SessionAttach& attach);
    void Detach(const internal::v1::SessionDetach& detach);
    void Input(const internal::v1::SessionInput& input);
    void LinkClosed(const LinkSink& sink);

    [[nodiscard]] const ZoneHostStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t session_count() const noexcept { return sessions_.size(); }
    void set_on_link_closed(std::function<void(GatewayLink&)> fn) {
        on_link_closed_ = std::move(fn);
    }

private:
    struct Session {
        LinkSink* sink = nullptr;
        common::EntityId entity;
        common::CharacterId character;
        common::OwnerEpoch epoch;
        // Mirror of the Backend's answer, kept current from kill and pickup responses so every
        // InventoryUpdate carries the full state.
        std::optional<codec::CharacterAssets> assets;
    };

    void Reject(LinkSink& sink, const std::string& session_id, internal::v1::RejectReason reason);
    static void SendToSession(const Session& session, const std::string& session_id,
                              const game::v1::ServerMessage& message);
    void SendAssets(const std::string& session_id, Session& session);
    void RemoveSession(const std::string& session_id, bool despawn);
    // The live session of a player entity, or nullptr once it left.
    [[nodiscard]] Session* SessionOf(common::EntityId entity);
    [[nodiscard]] codec::ZoneAuthority AuthorityFor(const Session& session) const;

    // Economy round trips.
    void RequestAssets(const std::string& session_id);
    void OnAssets(const std::string& session_id, common::EntityId entity,
                  const std::optional<backend::v1::GetCharacterAssetsResponse>& response);
    void RecordKill(const sim::KillRecord& kill);
    void OnKillRecorded(common::EntityId killer, common::KillId kill_id,
                        const std::optional<backend::v1::RecordKillResponse>& response);
    void SettlePickup(const sim::PickupReservation& pickup);
    void OnPickupSettled(const sim::PickupReservation& pickup,
                         const std::optional<backend::v1::PickupItemResponse>& response);

    sim::Zone& zone_;
    Identity identity_;
    EconomyGateway& economy_;
    std::function<bool()> authoritative_;
    std::function<void(GatewayLink&)> on_link_closed_;
    std::unordered_map<std::string, Session> sessions_;  // key: gateway_session_id wire bytes
    std::unordered_map<common::EntityId, std::string> session_by_entity_;
    ZoneHostStats stats_;
};

}  // namespace sa::zone
