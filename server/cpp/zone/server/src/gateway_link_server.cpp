#include "gateway_link_server.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <tuple>

namespace sa::zone {

GatewayLinkServer::GatewayLinkServer(asio::io_context& io, GatewayLinkServerConfig config,
                                     GatewayLink::Handler& handler)
    : acceptor_(io, asio::ip::tcp::endpoint{asio::ip::make_address(config.listen_address),
                                            config.listen_port}),
      config_(std::move(config)),
      handler_(handler) {}

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
                                                      config_.close_linger, handler_);
            links_.push_back(link);
            spdlog::info("gateway link accepted from {}", link->PeerAddress());
            link->Start();
        }
        Accept();
    });
}

}  // namespace sa::zone
