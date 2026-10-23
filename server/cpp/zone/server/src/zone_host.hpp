#pragma once

#include "sa/common/ids.hpp"
#include "sa/common/instance_id.hpp"
#include "sa/zone/sim/zone.hpp"

#include "codec.hpp"
#include "directory_client.hpp"
#include "economy_client.hpp"
#include "gateway_link.hpp"
#include "link_sink.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sa::zone {

struct ZoneHostStats {
    std::uint64_t attaches = 0;
    std::uint64_t rejects = 0;
    std::uint64_t detaches = 0;
    std::uint64_t evictions = 0;
    std::uint64_t inputs = 0;
    std::uint64_t snapshots_sent = 0;
    std::uint64_t snapshot_bytes = 0;      // link payload bytes of the snapshots sent
    std::uint64_t snapshots_deferred = 0;  // a link was at its budget; the view waited a tick
    std::uint64_t events_sent = 0;
    std::uint64_t bad_inputs = 0;
    // Economy round trips (see README "자산과 멱등성").
    std::uint64_t kills_recorded = 0;
    std::uint64_t kills_refused = 0;  // stale authority, content mismatch, unknown template
    std::uint64_t pickups_acquired = 0;
    std::uint64_t pickups_refused = 0;
    std::uint64_t backend_failures = 0;     // no answer at all, after the client's retries
    std::uint64_t not_owner_evictions = 0;  // the Backend says another zone owns the character
    std::uint64_t inventory_updates_sent = 0;
    // Placement round trips (see README "소유권과 lease").
    std::uint64_t checkpoints_saved = 0;
    std::uint64_t checkpoints_refused = 0;  // this zone no longer owns the character
    std::uint64_t transfers_completed = 0;
    std::uint64_t transfers_refused = 0;
    std::uint64_t stale_attaches = 0;  // an attach under a generation not newer than the live one
    std::uint64_t superseded_sessions = 0;  // a newer generation of the same character arrived
};

// Binds gateway sessions to simulated players and settles the zone's economy and placement
// requests with the Backend. Owns the session table and is the only code that translates between
// link messages, Backend RPCs and Zone calls. Single-threaded with the zone: the Backend clients
// post replies to the io_context that also runs the tick, so no reply races a tick.
//
// Split by concern across zone_host.cpp (sessions, input, output), zone_host_economy.cpp (assets,
// kills, pickups) and zone_host_placement.cpp (checkpoints, transfers).
class ZoneHost final : public GatewayLink::Handler {
public:
    struct Identity {
        common::InstanceId zone_instance;
        std::uint32_t simulation_hz = 0;
        std::uint32_t ticks_per_snapshot = 0;
        // How full a gateway link may get from one snapshot tick (ADR-0011). Sessions beyond it
        // wait for a later tick, longest-waiting first.
        std::size_t snapshot_budget_bytes = 0;
        std::uint32_t ticks_per_checkpoint = 0;
        std::uint32_t max_sessions = 0;
    };

    // `authoritative` answers whether the zone currently holds its Backend lease.
    ZoneHost(sim::Zone& zone, Identity identity, EconomyGateway& economy,
             DirectoryGateway& directory, std::function<bool()> authoritative);

    // Called after each simulation tick: delivers command results and combat events every tick,
    // forwards kills, pickups and departures to the Backend, snapshots and saves checkpoints at
    // the configured cadences.
    void OnTick(common::TickIndex tick);

    // Lease lost or shutdown: every session is told to leave and the players despawn.
    void EvictAll(internal::v1::EvictReason reason);
    void Evict(const common::InstanceId& session_id, internal::v1::EvictReason reason);

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
    // The deepest a gateway link's queue was after a snapshot tick since the last call.
    struct LinkPressure {
        std::size_t queue_peak_bytes = 0;
    };
    [[nodiscard]] LinkPressure TakeLinkPressure();
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
        std::uint64_t checkpoint_version = 0;  // grows with every save of this session
        // The position of the last save not known to have failed: an idle character is not saved
        // again and again.
        std::optional<common::Vec2> checkpointed;
        common::TickIndex last_snapshot;  // when this session last got one; zero = never
    };
    // A session as the snapshot pass sees it, grouped by link.
    struct SessionSlot {
        const common::InstanceId* id = nullptr;
        Session* session = nullptr;
    };

    void Reject(LinkSink& sink, const common::InstanceId& session_id,
                internal::v1::RejectReason reason);
    static void SendToSession(const Session& session, const common::InstanceId& session_id,
                              const game::v1::ServerMessage& message);
    void SendAssets(const common::InstanceId& session_id, Session& session);
    void RemoveSession(const common::InstanceId& session_id, bool despawn);
    void SendSnapshots(common::TickIndex tick);
    // The live session of a player entity, or nullptr once it left.
    [[nodiscard]] Session* SessionOf(common::EntityId entity);
    [[nodiscard]] codec::ZoneAuthority AuthorityFor(const Session& session) const;

    // Economy round trips (zone_host_economy.cpp).
    void RequestAssets(const common::InstanceId& session_id);
    void OnAssets(const common::InstanceId& session_id, common::EntityId entity,
                  const std::optional<backend::v1::GetCharacterAssetsResponse>& response);
    void RecordKill(const sim::KillRecord& kill);
    void OnKillRecorded(common::EntityId killer, common::KillId kill_id,
                        const std::optional<backend::v1::RecordKillResponse>& response);
    void SettlePickup(const sim::PickupReservation& pickup);
    void EvictNotOwner(const common::InstanceId& session_id);
    void OnPickupSettled(const sim::PickupReservation& pickup,
                         const std::optional<backend::v1::PickupItemResponse>& response);

    // Placement round trips (zone_host_placement.cpp).
    // The live session of a character, if it is already attached here.
    [[nodiscard]] std::optional<common::InstanceId> SessionOfCharacter(
        common::CharacterId character) const;
    void SaveCheckpoints();
    void SaveCheckpoint(const common::InstanceId& session_id, Session& session);
    void OnCheckpointSaved(const common::InstanceId& session_id, common::EntityId entity,
                           const std::optional<backend::v1::SaveCheckpointResponse>& response);
    void Transfer(const sim::ZoneTransferRequest& transfer);
    void OnTransferDecided(const sim::ZoneTransferRequest& transfer,
                           const std::optional<backend::v1::TransferCharacterResponse>& response);
    void HandOver(const sim::ZoneTransferRequest& transfer,
                  const backend::v1::TransferCharacterResponse& response);

    sim::Zone& zone_;
    Identity identity_;
    EconomyGateway& economy_;
    DirectoryGateway& directory_;
    std::function<bool()> authoritative_;
    std::function<void(GatewayLink&)> on_link_closed_;
    std::unordered_map<common::InstanceId, Session> sessions_;  // by gateway session id
    std::unordered_map<common::EntityId, common::InstanceId> session_by_entity_;
    // Reused by every snapshot pass, so steady state allocates nothing new.
    std::unordered_map<LinkSink*, std::vector<SessionSlot>> by_link_;
    std::string snapshot_scratch_;  // one encoded snapshot at a time
    std::size_t link_queue_peak_ = 0;
    std::uint64_t kills_minted_ = 0;  // sequence for KillIdFor; never repeats within the process
    ZoneHostStats stats_;
};

}  // namespace sa::zone
