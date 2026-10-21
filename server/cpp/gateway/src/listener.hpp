#pragma once

#include "client_session.hpp"

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sa::gateway {

struct ListenerConfig {
    std::string name;  // for logs: "tcp", "websocket"
    std::string address;
    std::uint16_t port = 0;
    std::uint32_t max_connections = 0;
};

// Accepts client connections, enforces the ceiling, and keeps the live sessions so shutdown can
// kick them. Session construction is injected so the listener knows nothing about transports or
// dependencies; one listener per transport (TCP, WebSocket) shares this class.
class Listener {
public:
    using SessionFactory = std::function<std::shared_ptr<ClientSession>(asio::ip::tcp::socket)>;

    Listener(asio::io_context& io, ListenerConfig config, SessionFactory factory);

    void Start();
    // Stops accepting and shuts every live session down.
    void Stop();

    void Forget(const ClientSession& session);
    [[nodiscard]] std::size_t open_sessions() const noexcept { return sessions_.size(); }
    [[nodiscard]] std::uint16_t local_port() const { return acceptor_.local_endpoint().port(); }

private:
    void Accept();

    asio::ip::tcp::acceptor acceptor_;
    ListenerConfig config_;
    SessionFactory factory_;
    std::vector<std::shared_ptr<ClientSession>> sessions_;
};

}  // namespace sa::gateway
