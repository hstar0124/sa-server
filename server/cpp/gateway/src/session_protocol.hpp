#pragma once

#include "sa/common/ids.hpp"
#include "sa/common/instance_id.hpp"
#include "sa/common/wall_clock.hpp"

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

// When a client message arrived, on both clocks the protocol needs: wall time for what crosses
// machines (ticket validity, the Pong's server time), monotonic time for the rate window, which
// must not jump when the wall clock is stepped.
struct ArrivalTime {
    std::chrono::steady_clock::time_point monotonic;
    common::WallTime wall;
};

struct SessionProtocolConfig {
    std::uint32_t max_messages_per_second = 0;
    // From accepting the connection (which includes a WebSocket handshake) to a valid Hello.
    // Without it, idle sockets could hold every connection slot.
    std::chrono::milliseconds hello_timeout{};
    // Once a client has spoken, the longest it may stay silent. Clients ping every second; a
    // connection that sends nothing is a peer that vanished without closing (a dead network, a
    // frozen tab), and it would otherwise hold the character's session until TCP gives up.
    std::chrono::milliseconds idle_timeout{};
};

// Per-connection state machine (see README "네트워크와 입장"), free of I/O so every transition
// is unit-tested. The owner (ClientSession) performs the returned effects.
//
//   AwaitingHello --Hello(valid ticket)--> Redeeming --GRANTED--> Attaching --Attached--> InZone
//   InZone --SessionTransfer--> Transferring --Attached (destination)--> InZone
//   any failure, kick, disconnect or zone eviction --> Closed
//
// Session messages count only when they come from the session's current zone: with several zones,
// a zone that does not hold the session (a stale one, or the origin after a transfer) must not
// speak for it. While Transferring the client does not know yet that its zone changed; its inputs
// are dropped rather than punished, and the destination's Welcome tells it where it is.
class SessionProtocol {
public:
    enum class State : std::uint8_t {
        kAwaitingHello,
        kRedeeming,
        kAttaching,
        kInZone,
        kTransferring,
        kClosed
    };

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
    [[nodiscard]] Reaction OnClientMessage(const game::v1::ClientMessage& message, ArrivalTime now);
    // Redeem answers are delivered even after the session closed: a grant that arrives late
    // created a Backend session that only this gateway can end.
    [[nodiscard]] Reaction OnRedeemResult(const backend::v1::RedeemEntryTicketResponse& response);
    [[nodiscard]] Reaction OnRedeemFailed();  // RPC error or deadline: the outcome is unknown
    // `from` is the zone whose link carried the message; anything not from the current zone is
    // ignored.
    [[nodiscard]] Reaction OnZoneAttached(common::ZoneId from, common::EntityId entity);
    [[nodiscard]] Reaction OnZoneRejected(common::ZoneId from, internal::v1::RejectReason reason);
    [[nodiscard]] Reaction OnZoneEvicted(common::ZoneId from, internal::v1::EvictReason reason);
    // The origin zone released the character after the Backend moved it (ADR-0010).
    [[nodiscard]] Reaction OnZoneTransfer(common::ZoneId from,
                                          const internal::v1::SessionTransfer& transfer);
    [[nodiscard]] Reaction OnZoneLinkDown();
    // Output from the zone for this session; passes through only while in the zone.
    [[nodiscard]] Reaction OnZoneOutput(common::ZoneId from,
                                        const game::v1::ServerMessage& message);
    [[nodiscard]] Reaction OnClientDisconnected();
    // The hello timeout passed; kicks a connection that has not sent a valid Hello yet.
    [[nodiscard]] Reaction OnHelloDeadline();
    // The idle timer fired: kicks a client that has sent nothing since `now - idle_timeout`.
    [[nodiscard]] Reaction OnIdleCheck(std::chrono::steady_clock::time_point now);
    // When the client counts as idle unless something arrives first; nothing before it spoke.
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> IdleDeadline() const;
    // The gateway ends the session itself: shutdown, or the loss of its own lease.
    [[nodiscard]] Reaction OnServerClose(game::v1::KickReason reason);

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] std::optional<common::ZoneId> zone() const noexcept { return zone_; }
    [[nodiscard]] const common::InstanceId& session_id() const noexcept { return session_id_; }

private:
    [[nodiscard]] Reaction OnHello(const game::v1::Hello& hello, common::WallTime now);
    [[nodiscard]] Reaction Kick(
        game::v1::KickReason reason,
        internal::v1::DetachReason detach_reason = internal::v1::DETACH_REASON_PROTOCOL_ERROR);
    // Transitions to Closed and emits the cleanup effects the current state requires.
    void Teardown(Reaction& reaction, internal::v1::DetachReason detach_reason,
                  std::string end_reason);
    [[nodiscard]] bool WithinRateLimit(std::chrono::steady_clock::time_point now);
    [[nodiscard]] bool FromCurrentZone(common::ZoneId from) const noexcept { return zone_ == from; }
    [[nodiscard]] bool AttachedOrAttaching() const noexcept {
        return state_ == State::kInZone || state_ == State::kAttaching ||
               state_ == State::kTransferring;
    }

    const EntryTicketVerifier& verifier_;
    SessionProtocolConfig config_;
    common::InstanceId session_id_;
    State state_ = State::kAwaitingHello;
    std::optional<VerifiedTicket> ticket_;  // identity the Backend confirmed, used for the attach
    // The attach as the Backend's grant shaped it; a transfer sends it again with the new
    // generation and the arrival position.
    std::optional<internal::v1::SessionAttach> attach_;
    std::optional<common::ZoneId> zone_;
    bool session_granted_ = false;   // the Backend holds a live session row for us
    bool redeem_in_flight_ = false;  // asked the Backend; its answer has not arrived
    // Fixed one-second windows: up to max_messages_per_second per window. A burst that straddles
    // a window boundary can reach twice that, which the limit tolerates.
    std::optional<std::chrono::steady_clock::time_point> last_heard_;  // any client message
    std::chrono::steady_clock::time_point window_start_;
    std::uint32_t messages_in_window_ = 0;
};

}  // namespace sa::gateway
