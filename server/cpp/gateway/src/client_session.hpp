#pragma once

#include "sa/common/instance_id.hpp"
#include "sa/common/message_connection.hpp"

#include "directory_client.hpp"
#include "session_protocol.hpp"
#include "session_registry.hpp"

#include <functional>
#include <memory>

namespace sa::gateway {

// One client connection. Owns the socket (via FramedConnection) and the protocol state machine;
// everything it does to the outside world is an effect the protocol asked for.
class ClientSession : public std::enable_shared_from_this<ClientSession> {
public:
    struct Dependencies {
        const EntryTicketVerifier& verifier;
        SessionProtocolConfig protocol;
        DirectoryClient& directory;
        ZoneRouter& zones;
        SessionRegistry& registry;
    };

    ClientSession(std::shared_ptr<common::MessageConnection> connection, Dependencies deps);

    void Start();
    // Server-initiated close (shutdown): kick, detach, end session.
    void Shutdown();
    // Invoked once when the socket has closed and all cleanup effects were issued.
    void set_on_finished(std::function<void()> fn) { on_finished_ = std::move(fn); }

    // From the zone, via SessionRegistry.
    void OnZoneAttached(const internal::v1::SessionAttached& attached);
    void OnZoneRejected(const internal::v1::SessionRejected& rejected);
    void OnZoneEvicted(const internal::v1::SessionEvicted& evicted);
    void OnZoneOutput(const game::v1::ServerMessage& message);
    void OnZoneLinkDown(common::ZoneId zone);

    [[nodiscard]] const common::InstanceId& id() const noexcept { return id_; }

private:
    void OnFrame(std::span<const std::byte> payload);
    void OnClosed(const std::error_code& reason);
    void Apply(SessionProtocol::Reaction reaction);
    void Perform(SessionProtocol::RedeemTicket effect);
    void Perform(const SessionProtocol::AttachToZone& effect);
    void Perform(const SessionProtocol::ForwardToZone& effect);
    void Perform(const SessionProtocol::DetachFromZone& effect);
    void Perform(SessionProtocol::EndSession effect);

    common::InstanceId id_;
    std::shared_ptr<common::MessageConnection> connection_;
    Dependencies deps_;
    SessionProtocol protocol_;
    std::function<void()> on_finished_;
    bool finished_ = false;
};

}  // namespace sa::gateway
