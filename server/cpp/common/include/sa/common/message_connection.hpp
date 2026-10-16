#pragma once

#include <google/protobuf/message_lite.h>

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <system_error>

namespace sa::common {

// A connection that carries whole protobuf messages, whatever the wire format underneath
// (ADR-0004: length-prefixed TCP for native clients, WebSocket for the browser viewer).
// Session logic depends on this and never on the transport.
class MessageConnection {
public:
    struct Callbacks {
        std::function<void(std::span<const std::byte>)> on_message;  // one serialized message
        std::function<void(const std::error_code&)> on_closed;       // error_code{} = orderly close
    };

    virtual ~MessageConnection() = default;

    virtual void Start(Callbacks callbacks) = 0;
    // Queues one message. Returns false (and sends nothing) if it cannot be framed.
    virtual bool Send(const google::protobuf::MessageLite& message) = 0;
    // Flushes queued messages, then closes. Idempotent.
    virtual void Close() = 0;
    // Drops queued messages and closes now.
    virtual void Abort() = 0;
    [[nodiscard]] virtual bool is_open() const = 0;
    [[nodiscard]] virtual std::string PeerAddress() const = 0;
};

}  // namespace sa::common
