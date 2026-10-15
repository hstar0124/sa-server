#pragma once

#include <internal/zone_link.pb.h>

namespace sa::zone {

// Where ZoneHost writes messages for one gateway. GatewayLink implements it over a socket;
// tests implement it with a vector.
class LinkSink {
public:
    virtual ~LinkSink() = default;
    virtual void Send(const internal::v1::ZoneToGateway& message) = 0;
};

}  // namespace sa::zone
