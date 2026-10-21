#pragma once

#include <google/protobuf/message_lite.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace sa::common {

// Why a message was not queued.
enum class SendError : std::uint8_t {
    kClosed,           // the connection is closing or closed; whatever happens next is its close
    kSerializeFailed,  // the message did not serialize
    kTooLarge,         // larger than the connection's frame limit
};

[[nodiscard]] constexpr std::string_view Describe(SendError error) noexcept {
    switch (error) {
        case SendError::kClosed:
            return "connection closed";
        case SendError::kSerializeFailed:
            return "message did not serialize";
        case SendError::kTooLarge:
            return "message exceeds the frame limit";
    }
    return "unknown send error";
}

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
    // Queues one message, or sends nothing and says why.
    [[nodiscard]] virtual std::expected<void, SendError> Send(
        const google::protobuf::MessageLite& message) = 0;
    // Flushes queued messages, then closes. Idempotent.
    virtual void Close() = 0;
    // Drops queued messages and closes now.
    virtual void Abort() = 0;
    [[nodiscard]] virtual bool is_open() const = 0;
    [[nodiscard]] virtual std::string PeerAddress() const = 0;
};

}  // namespace sa::common
