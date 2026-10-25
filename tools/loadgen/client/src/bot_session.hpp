#pragma once

#include "sa/bot/observation.hpp"
#include "sa/common/framed_connection.hpp"

#include "profile.hpp"
#include "report.hpp"
#include "world_model.hpp"

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>

#include <game/client_messages.pb.h>
#include <game/server_messages.pb.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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

    BotSession(asio::io_context& io, const LoadProfile& profile, std::unique_ptr<bot::Brain> brain,
               Target target, BotReport& report);

    void Start();
    void Stop();  // graceful: stop timers, close the socket

private:
    void OnConnected();
    void OnFrame(std::span<const std::byte> payload);
    void OnClosed(const std::error_code& ec);
    void OnServerMessage(const game::v1::ServerMessage& message);
    void ScheduleTick();
    void Tick();
    void SendChangeZone(common::PortalId portal);
    void SendTradeStep(const bot::TradeIntent& step);
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
    std::unique_ptr<bot::Brain> brain_;
    WorldModel world_;
    std::uint32_t next_input_seq_ = 1;
    std::uint64_t next_request_id_ = 0;
    // The ChangeZone the zone has not decided yet. Asking again repeats it under the same request
    // id, as the protocol asks of a client: after a lost answer the zone then asks the Backend
    // about the same transfer instead of starting another.
    std::optional<common::RequestId> departure_;
    // The trade step the zone has not decided yet, as sent. The zone takes one at a time; an
    // UNKNOWN answer sends it again under the same id after the trader's retry interval.
    struct HeldTradeStep {
        common::RequestId request;
        game::v1::ClientMessage message;
        std::optional<std::chrono::steady_clock::time_point> resend_at;
    };
    std::optional<HeldTradeStep> trade_step_;
    void SendHeldTradeStep(HeldTradeStep& step);
    std::uint32_t ping_nonce_ = 0;
    std::chrono::steady_clock::time_point ping_sent_at_;
    bool stopped_ = false;
};

}  // namespace sa::loadgen
