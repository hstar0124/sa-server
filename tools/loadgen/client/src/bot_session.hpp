#pragma once

#include "sa/bot/wanderer.hpp"
#include "sa/common/framed_connection.hpp"

#include "profile.hpp"
#include "report.hpp"
#include "world_model.hpp"

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>

#include <game/server_messages.pb.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace sa::loadgen {

// One bot on the wire: connect, Hello, then decide and send MoveInput at the client tick rate
// while folding snapshots into the WorldModel. Mirrors what a real client does minus rendering.
class BotSession : public std::enable_shared_from_this<BotSession> {
public:
    struct Target {
        std::string host;
        std::uint16_t port = 0;
        std::vector<std::byte> signed_ticket;
    };

    BotSession(asio::io_context& io, const LoadProfile& profile, std::uint64_t seed, Target target,
               BotReport& report);

    void Start();
    void Stop();  // graceful: stop timers, close the socket

private:
    void OnConnected();
    void OnFrame(std::span<const std::byte> payload);
    void OnClosed(const std::error_code& ec);
    void OnServerMessage(const game::v1::ServerMessage& message);
    void ScheduleTick();
    void Tick();
    void SchedulePing();
    void Ping();
    void Fail(std::string what);

    asio::io_context& io_;
    const LoadProfile& profile_;
    Target target_;
    BotReport& report_;
    asio::ip::tcp::resolver resolver_;
    asio::steady_timer tick_timer_;
    asio::steady_timer ping_timer_;
    asio::steady_timer connect_timer_;
    std::shared_ptr<common::FramedConnection> connection_;
    bot::Wanderer brain_;
    WorldModel world_;
    std::uint32_t next_input_seq_ = 1;
    std::uint32_t ping_nonce_ = 0;
    std::chrono::steady_clock::time_point ping_sent_at_;
    bool stopped_ = false;
};

}  // namespace sa::loadgen
