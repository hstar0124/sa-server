#pragma once

#include "sa/common/framing.hpp"

#include "gateway_link.hpp"

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sa::zone {

struct GatewayLinkServerConfig {
    std::string listen_address;
    std::uint16_t listen_port = 0;
    std::uint32_t max_gateways = 0;
    common::FrameLimits frame_limits;
    std::chrono::milliseconds close_linger{};     // bounds closing a link at shutdown
    std::chrono::milliseconds silence_timeout{};  // a link that carries nothing this long is dead
    // TCP_MAXSEG on the listening socket: accepted links inherit it and advertise it, so it bounds
    // the segments both ways. The kernel refuses values outside its own range at startup.
    std::uint32_t max_segment_bytes = 0;
};

// Accepts gateway connections and keeps the live links so shutdown can close them.
class GatewayLinkServer {
public:
    GatewayLinkServer(asio::io_context& io, GatewayLinkServerConfig config,
                      GatewayLink::Handler& handler);

    void Start();
    void Stop();

    void Forget(const GatewayLink& link);
    [[nodiscard]] std::size_t open_links() const noexcept { return links_.size(); }
    // The longest gap between two frames on any link since the last call.
    [[nodiscard]] std::chrono::milliseconds TakeLongestGap();

private:
    void Accept();

    asio::ip::tcp::acceptor acceptor_;
    GatewayLinkServerConfig config_;
    GatewayLink::Handler& handler_;
    std::vector<std::shared_ptr<GatewayLink>> links_;
};

}  // namespace sa::zone
