#pragma once

#include "sa/common/ids.hpp"
#include "sa/common/instance_id.hpp"
#include "sa/zone/sim/zone.hpp"

#include "gateway_link.hpp"
#include "link_sink.hpp"

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace sa::zone {

struct ZoneHostStats {
    std::uint64_t attaches = 0;
    std::uint64_t rejects = 0;
    std::uint64_t detaches = 0;
    std::uint64_t evictions = 0;
    std::uint64_t inputs = 0;
    std::uint64_t snapshots_sent = 0;
    std::uint64_t bad_inputs = 0;
};

// Binds gateway sessions to simulated players. Owns the session table and is the only code
// that translates between link messages and Zone calls. Single-threaded with the zone.
class ZoneHost final : public GatewayLink::Handler {
public:
    struct Identity {
        common::InstanceId zone_instance;
        std::uint32_t simulation_hz = 0;
        std::uint32_t ticks_per_snapshot = 0;
        std::uint32_t max_sessions = 0;
    };

    // `authoritative` answers whether the zone currently holds its Backend lease.
    ZoneHost(sim::Zone& zone, Identity identity, std::function<bool()> authoritative);

    // Called after each simulation tick; sends snapshots at the configured cadence.
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
    };

    void Reject(LinkSink& sink, const std::string& session_id, internal::v1::RejectReason reason);
    static void SendToSession(const Session& session, const std::string& session_id,
                              const game::v1::ServerMessage& message);
    void RemoveSession(const std::string& session_id, bool despawn);

    sim::Zone& zone_;
    Identity identity_;
    std::function<bool()> authoritative_;
    std::function<void(GatewayLink&)> on_link_closed_;
    std::unordered_map<std::string, Session> sessions_;  // key: gateway_session_id wire bytes
    ZoneHostStats stats_;
};

}  // namespace sa::zone
