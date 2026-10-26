#include "gateway_link_server.hpp"

#include <asio/socket_base.hpp>
#include <spdlog/spdlog.h>

#include <netinet/tcp.h>

#include <algorithm>
#include <tuple>

namespace sa::zone {
namespace {

// Asio names no portable option for this one.
using MaxSegment = asio::detail::socket_option::integer<IPPROTO_TCP, TCP_MAXSEG>;

}  // namespace

GatewayLinkServer::GatewayLinkServer(asio::io_context& io, GatewayLinkServerConfig config,
                                     GatewayLink::Handler& handler)
    : acceptor_(io, asio::ip::tcp::endpoint{asio::ip::make_address(config.listen_address),
                                            config.listen_port}),
      config_(std::move(config)),
      handler_(handler) {
    // A link's receive window starts small and grows with traffic. While it is smaller than one
    // segment the peer sends nothing but window probes, backing off to seconds: the link falls
    // silent under load and is closed as dead (docs/issues/021). Smaller segments always fit.
    acceptor_.set_option(MaxSegment{static_cast<int>(config_.max_segment_bytes)});
}

void GatewayLinkServer::Start() {
    spdlog::info("gateway link listening on {}:{} max_gateways={}", config_.listen_address,
                 config_.listen_port, config_.max_gateways);
    Accept();
}

void GatewayLinkServer::Stop() {
    std::error_code ignored;
    std::ignore = acceptor_.close(ignored);
    for (const auto& link : links_) {
        link->Close();
    }
}

void GatewayLinkServer::Forget(const GatewayLink& link) {
    std::erase_if(links_, [&](const auto& l) { return l.get() == &link; });
}

void GatewayLinkServer::Accept() {
    acceptor_.async_accept([this](const std::error_code& ec, asio::ip::tcp::socket socket) {
        if (ec == asio::error::operation_aborted) {
            return;
        }
        if (ec) {
            spdlog::warn("gateway link accept failed: {}", ec.message());
        } else if (links_.size() >= config_.max_gateways) {
            spdlog::warn("gateway link ceiling {} reached, refusing peer", config_.max_gateways);
            std::error_code ignored;
            std::ignore = socket.close(ignored);
        } else {
            auto link = std::make_shared<GatewayLink>(std::move(socket), config_.frame_limits,
                                                      config_.close_linger, config_.silence_timeout,
                                                      handler_);
            links_.push_back(link);
            spdlog::info("gateway link accepted from {}", link->PeerAddress());
            link->Start();
        }
        Accept();
    });
}

std::chrono::milliseconds GatewayLinkServer::TakeLongestGap() {
    std::chrono::milliseconds longest{};
    for (const auto& link : links_) {
        longest = std::max(longest, link->TakeLongestGap());
    }
    return longest;
}

}  // namespace sa::zone
