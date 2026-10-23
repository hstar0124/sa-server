#pragma once

#include <internal/zone_link.pb.h>

#include <cstddef>

namespace sa::zone {

// Where ZoneHost writes messages for one gateway. GatewayLink implements it over a socket;
// tests implement it with a vector.
class LinkSink {
public:
    virtual ~LinkSink() = default;
    virtual void Send(const internal::v1::ZoneToGateway& message) = 0;
    // Bytes sent but not yet taken by the transport: the host's backpressure signal.
    [[nodiscard]] virtual std::size_t QueuedBytes() const = 0;
};

}  // namespace sa::zone
