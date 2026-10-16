#include "listener.hpp"

#include <spdlog/spdlog.h>

#include <tuple>

namespace sa::gateway {

Listener::Listener(asio::io_context& io, ListenerConfig config, SessionFactory factory)
    : acceptor_(io, asio::ip::tcp::endpoint{asio::ip::make_address(config.address), config.port}),
      config_(std::move(config)),
      factory_(std::move(factory)) {}

void Listener::Start() {
    spdlog::info("listening on {}:{} max_connections={}", config_.address, config_.port,
                 config_.max_connections);
    Accept();
}

void Listener::Stop() {
    std::error_code ignored;
    std::ignore = acceptor_.close(ignored);
    // Copy: a session's shutdown removes it from sessions_ through Forget().
    const auto live = sessions_;
    for (const auto& session : live) {
        session->Shutdown();
    }
}

void Listener::Forget(const ClientSession& session) {
    std::erase_if(sessions_, [&](const auto& s) { return s.get() == &session; });
}

void Listener::Accept() {
    acceptor_.async_accept([this](const std::error_code& ec, asio::ip::tcp::socket socket) {
        if (ec == asio::error::operation_aborted) {
            return;
        }
        if (ec) {
            spdlog::warn("accept failed: {}", ec.message());
        } else if (sessions_.size() >= config_.max_connections) {
            spdlog::warn("{} connection ceiling {} reached, refusing peer", config_.name,
                         config_.max_connections);
            std::error_code ignored;
            std::ignore = socket.close(ignored);
        } else {
            auto session = factory_(std::move(socket));
            session->set_on_finished([this, raw = session.get()] { Forget(*raw); });
            sessions_.push_back(session);
            session->Start();
        }
        Accept();
    });
}

}  // namespace sa::gateway
