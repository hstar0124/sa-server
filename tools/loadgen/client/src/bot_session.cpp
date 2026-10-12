#include "bot_session.hpp"

#include "sa/common/bytes.hpp"
#include "sa/common/protocol_limits.hpp"
#include "sa/common/wall_clock.hpp"

#include <asio/connect.hpp>
#include <spdlog/spdlog.h>

#include <game/client_messages.pb.h>

#include <string>

namespace sa::loadgen {

BotSession::BotSession(asio::io_context& io, const LoadProfile& profile, std::uint64_t seed,
                       Target target, BotReport& report)
    : io_(io),
      profile_(profile),
      target_(std::move(target)),
      report_(report),
      resolver_(io),
      tick_timer_(io),
      ping_timer_(io),
      connect_timer_(io),
      brain_(seed, profile.wanderer) {}

void BotSession::Start() {
    connect_timer_.expires_after(profile_.connect_timeout);
    connect_timer_.async_wait([self = shared_from_this()](const std::error_code& ec) {
        if (!ec && !self->report_.connected) {
            self->Fail("connect timeout");
        }
    });
    resolver_.async_resolve(
        target_.host, std::to_string(target_.port),
        [self = shared_from_this()](const std::error_code& ec,
                                    const asio::ip::tcp::resolver::results_type& results) {
            if (self->stopped_) {
                return;
            }
            if (ec) {
                self->Fail("resolve: " + ec.message());
                return;
            }
            auto socket = std::make_shared<asio::ip::tcp::socket>(self->io_);
            asio::async_connect(
                *socket, results,
                [self, socket](const std::error_code& cec, const asio::ip::tcp::endpoint&) {
                    if (self->stopped_) {
                        return;
                    }
                    if (cec) {
                        self->Fail("connect: " + cec.message());
                        return;
                    }
                    self->connection_ = std::make_shared<common::FramedConnection>(
                        std::move(*socket), common::FrameLimits{});
                    self->OnConnected();
                });
        });
}

void BotSession::Stop() {
    if (stopped_) {
        return;
    }
    stopped_ = true;
    tick_timer_.cancel();
    ping_timer_.cancel();
    connect_timer_.cancel();
    if (connection_) {
        connection_->Close();
    }
}

void BotSession::Fail(std::string what) {
    if (report_.failure.empty()) {
        report_.failure = std::move(what);
    }
    Stop();
}

void BotSession::OnConnected() {
    report_.connected = true;
    connect_timer_.cancel();
    connection_->Start(common::MessageConnection::Callbacks{
        .on_message = [self =
                           shared_from_this()](std::span<const std::byte> p) { self->OnFrame(p); },
        .on_closed = [self = shared_from_this()](const std::error_code& ec) { self->OnClosed(ec); },
    });
    game::v1::ClientMessage hello;
    hello.mutable_hello()->set_protocol_version(common::protocol::kCurrentVersion);
    hello.mutable_hello()->set_entry_ticket(common::ToWireString(target_.signed_ticket));
    connection_->Send(hello);
}

void BotSession::OnFrame(std::span<const std::byte> payload) {
    game::v1::ServerMessage message;
    if (!message.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
        Fail("unparseable server frame");
        return;
    }
    OnServerMessage(message);
}

void BotSession::OnServerMessage(const game::v1::ServerMessage& message) {
    using game::v1::ServerMessage;
    switch (message.body_case()) {
        case ServerMessage::kWelcome:
            world_.OnWelcome(message.welcome());
            report_.welcomed = true;
            ScheduleTick();
            SchedulePing();
            return;
        case ServerMessage::kWorldSnapshot:
            world_.OnSnapshot(message.world_snapshot());
            report_.snapshots = world_.snapshots();
            report_.max_visible = world_.max_visible();
            report_.last_acked_input = world_.last_acked_input();
            report_.integrity = world_.integrity();
            return;
        case ServerMessage::kPong:
            if (message.pong().nonce() == ping_nonce_) {
                report_.ping_rtts.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - ping_sent_at_));
            }
            return;
        case ServerMessage::kKick:
            report_.kicked = message.kick().reason();
            spdlog::info("bot {} kicked: {}", report_.index,
                         game::v1::KickReason_Name(message.kick().reason()));
            Stop();
            return;
        case ServerMessage::kCommandResult:
            return;  // stage 5
        case ServerMessage::BODY_NOT_SET:
            Fail("empty server message");
            return;
    }
}

void BotSession::OnClosed(const std::error_code& ec) {
    if (!stopped_ && ec) {
        Fail("connection lost: " + ec.message());
    }
    Stop();
}

void BotSession::ScheduleTick() {
    tick_timer_.expires_after(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds{1}) /
        profile_.client_tick_hz);
    tick_timer_.async_wait([self = shared_from_this()](const std::error_code& ec) {
        if (ec || self->stopped_) {
            return;
        }
        self->Tick();
        self->ScheduleTick();
    });
}

void BotSession::Tick() {
    const bot::Observation observation{.self_position = world_.self_position(),
                                       .map_size = world_.map_size(),
                                       .visible = world_.Visible(),
                                       .tick = world_.server_tick()};
    const common::Vec2 direction = brain_.Decide(observation);
    game::v1::ClientMessage move;
    auto* input = move.mutable_move_input();
    input->set_input_seq(next_input_seq_++);
    input->mutable_direction()->set_x(direction.x);
    input->mutable_direction()->set_y(direction.y);
    input->set_client_tick(static_cast<std::uint32_t>(world_.server_tick()));
    if (connection_->Send(move)) {
        ++report_.inputs_sent;
    }
}

void BotSession::SchedulePing() {
    ping_timer_.expires_after(profile_.ping_interval);
    ping_timer_.async_wait([self = shared_from_this()](const std::error_code& ec) {
        if (ec || self->stopped_) {
            return;
        }
        self->Ping();
        self->SchedulePing();
    });
}

void BotSession::Ping() {
    game::v1::ClientMessage ping;
    ping.mutable_ping()->set_nonce(++ping_nonce_);
    ping.mutable_ping()->set_client_time_ms(common::UnixMillisNow());
    ping_sent_at_ = std::chrono::steady_clock::now();
    connection_->Send(ping);
}

}  // namespace sa::loadgen
