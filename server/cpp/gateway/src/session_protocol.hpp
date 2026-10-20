#pragma once

#include "sa/common/ids.hpp"
#include "sa/common/instance_id.hpp"

#include "entry_ticket_verifier.hpp"

#include <backend/world_directory.pb.h>
#include <game/client_messages.pb.h>
#include <game/server_messages.pb.h>
#include <internal/zone_link.pb.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace sa::gateway {

struct SessionProtocolConfig {
    std::uint32_t max_messages_per_second = 0;
    // From accepting the connection (which includes a WebSocket handshake) to a valid Hello.
    // Without it, idle sockets could hold every connection slot.
    std::chrono::milliseconds hello_timeout{};
};

// Per-connection state machine (see README "네트워크와 입장"), free of I/O so every transition
// is unit-tested. The owner (ClientSession) performs the returned effects.
//
//   AwaitingHello --Hello(valid ticket)--> Redeeming --GRANTED--> Attaching --Attached--> InZone
//   any failure, kick, disconnect or zone eviction --> Closed
class SessionProtocol {
public:
    enum class State : std::uint8_t { kAwaitingHello, kRedeeming, kAttaching, kInZone, kClosed };

    // ---- effects the owner must carry out ----
    struct RedeemTicket {
        VerifiedTicket ticket;
    };
    struct AttachToZone {
        common::ZoneId zone;
        internal::v1::SessionAttach attach;
    };
    struct ForwardToZone {
        game::v1::ClientMessage message;
    };
    struct DetachFromZone {
        common::ZoneId zone;
        internal::v1::DetachReason reason = internal::v1::DETACH_REASON_UNSPECIFIED;
    };
    struct EndSession {
        std::string reason;
    };
    using Effect =
        std::variant<RedeemTicket, AttachToZone, ForwardToZone, DetachFromZone, EndSession>;

    struct Reaction {
        std::vector<game::v1::ServerMessage> replies;
        std::vector<Effect> effects;
        bool close = false;
    };

    SessionProtocol(const EntryTicketVerifier& verifier, SessionProtocolConfig config,
                    common::InstanceId session_id);

    // ---- inputs ----
    [[nodiscard]] Reaction OnClientMessage(const game::v1::ClientMessage& message,
                                           std::uint64_t now_ms);
    // Redeem answers are delivered even after the session closed: a grant that arrives late
    // created a Backend session that only this gateway can end.
    [[nodiscard]] Reaction OnRedeemResult(const backend::v1::RedeemEntryTicketResponse& response);
    [[nodiscard]] Reaction OnRedeemFailed();  // RPC error or deadline: the outcome is unknown
    [[nodiscard]] Reaction OnZoneAttached(common::EntityId entity);
    [[nodiscard]] Reaction OnZoneRejected(internal::v1::RejectReason reason);
    [[nodiscard]] Reaction OnZoneEvicted(internal::v1::EvictReason reason);
    [[nodiscard]] Reaction OnZoneLinkDown();
    // Output from the zone for this session; passes through only while in the zone.
    [[nodiscard]] Reaction OnZoneOutput(const game::v1::ServerMessage& message);
    [[nodiscard]] Reaction OnClientDisconnected();
    // The hello timeout passed; kicks a connection that has not sent a valid Hello yet.
    [[nodiscard]] Reaction OnHelloDeadline();
    // The gateway ends the session itself: shutdown, or the loss of its own lease.
    [[nodiscard]] Reaction OnServerClose(game::v1::KickReason reason);

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] std::optional<common::ZoneId> zone() const noexcept { return zone_; }
    [[nodiscard]] const common::InstanceId& session_id() const noexcept { return session_id_; }

private:
    [[nodiscard]] Reaction OnHello(const game::v1::Hello& hello, std::uint64_t now_ms);
    [[nodiscard]] Reaction Kick(
        game::v1::KickReason reason,
        internal::v1::DetachReason detach_reason = internal::v1::DETACH_REASON_PROTOCOL_ERROR);
    // Transitions to Closed and emits the cleanup effects the current state requires.
    void Teardown(Reaction& reaction, internal::v1::DetachReason detach_reason,
                  std::string end_reason);
    [[nodiscard]] bool WithinRateLimit(std::uint64_t now_ms);

    const EntryTicketVerifier& verifier_;
    SessionProtocolConfig config_;
    common::InstanceId session_id_;
    State state_ = State::kAwaitingHello;
    std::optional<VerifiedTicket> ticket_;  // identity the Backend confirmed, used for the attach
    std::optional<common::ZoneId> zone_;
    bool session_granted_ = false;   // the Backend holds a live session row for us
    bool redeem_in_flight_ = false;  // asked the Backend; its answer has not arrived
    // Fixed one-second windows: up to max_messages_per_second per window. A burst that straddles
    // a window boundary can reach twice that, which the limit tolerates.
    std::uint64_t window_start_ms_ = 0;
    std::uint32_t messages_in_window_ = 0;
};

}  // namespace sa::gateway
