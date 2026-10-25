#include "bot_session.hpp"

#include "sa/common/bytes.hpp"
#include "sa/common/protocol_limits.hpp"
#include "sa/common/wall_clock.hpp"

#include <asio/connect.hpp>
#include <spdlog/spdlog.h>

#include <game/client_messages.pb.h>

#include <string>
#include <tuple>

namespace sa::loadgen {

BotSession::BotSession(asio::io_context& io, const LoadProfile& profile,
                       std::unique_ptr<bot::Brain> brain, Target target, BotReport& report)
    : io_(io),
      profile_(profile),
      target_(std::move(target)),
      report_(report),
      resolver_(io),
      tick_timer_(io),
      ping_timer_(io),
      connect_timer_(io),
      brain_(std::move(brain)) {}

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
                    // No close linger: the gateway closes its side as soon as it sees ours.
                    self->connection_ = std::make_shared<common::FramedConnection>(
                        std::move(*socket), common::FrameLimits{}, std::nullopt);
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
    if (const auto sent = connection_->Send(hello); !sent) {
        Fail(std::string{"hello not sent: "} + std::string{common::Describe(sent.error())});
    }
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
        case ServerMessage::kWelcome: {
            const bool first = !world_.welcomed();
            trade_step_.reset();  // a new zone: the old one's steps are answered there or never
            world_.OnWelcome(message.welcome());
            report_.welcomed = true;
            report_.travel = world_.travel();
            if (first) {
                ScheduleTick();
                SchedulePing();
            } else {
                departure_.reset();  // the crossing landed
                spdlog::debug("bot {} now in zone {}", report_.index, world_.zone().value());
            }
            return;
        }
        case ServerMessage::kWorldSnapshot:
            world_.OnSnapshot(message.world_snapshot());
            report_.snapshots = world_.snapshots();
            report_.max_visible = world_.max_visible();
            report_.max_visible_players = world_.max_visible_players();
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
            if (const auto status = message.command_result().status();
                departure_ == common::RequestId{message.command_result().request_id()} &&
                (status == game::v1::COMMAND_STATUS_ACCEPTED ||
                 status == game::v1::COMMAND_STATUS_REJECTED)) {
                departure_.reset();  // decided; the next attempt is a new request
            }
            if (trade_step_ &&
                trade_step_->request == common::RequestId{message.command_result().request_id()}) {
                switch (message.command_result().status()) {
                    case game::v1::COMMAND_STATUS_ACCEPTED:
                    case game::v1::COMMAND_STATUS_REJECTED:
                        trade_step_.reset();  // decided; the brain may take the next step
                        break;
                    case game::v1::COMMAND_STATUS_UNKNOWN:
                        trade_step_->resend_at =
                            std::chrono::steady_clock::now() + profile_.trader.request_retry;
                        break;
                    case game::v1::COMMAND_STATUS_PENDING:  // the Backend is deciding
                    case game::v1::COMMAND_STATUS_UNSPECIFIED:
                    case game::v1::CommandStatus_INT_MIN_SENTINEL_DO_NOT_USE_:
                    case game::v1::CommandStatus_INT_MAX_SENTINEL_DO_NOT_USE_:
                        break;
                }
            }
            world_.OnCommandResult(message.command_result());
            report_.combat = world_.combat();
            report_.loot = world_.loot();
            report_.travel = world_.travel();
            report_.integrity = world_.integrity();
            return;
        case ServerMessage::kCombatEvent:
            world_.OnCombatEvent(message.combat_event());
            report_.combat = world_.combat();
            return;
        case ServerMessage::kInventoryUpdate:
            world_.OnInventoryUpdate(message.inventory_update());
            report_.loot = world_.loot();
            report_.integrity = world_.integrity();
            return;
        case ServerMessage::kTradeUpdate:
            world_.OnTradeUpdate(message.trade_update());
            report_.trades = world_.trades();
            report_.integrity = world_.integrity();
            return;
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
                                       .self_hp = world_.self_hp(),
                                       .self_max_hp = world_.self_max_hp(),
                                       .visible = world_.Visible(),
                                       .portals = world_.portals(),
                                       .zone = world_.zone(),
                                       .tick = common::TickIndex{world_.server_tick()},
                                       .tick_hz = world_.simulation_hz(),
                                       .gold = world_.loot().gold,
                                       .trade = world_.trade(),
                                       .trade_step_pending = trade_step_.has_value()};
    const bot::Intent intent = brain_->Decide(observation);
    world_.ForgetClosedTrade();
    game::v1::ClientMessage move;
    auto* input = move.mutable_move_input();
    input->set_input_seq(next_input_seq_++);
    input->mutable_direction()->set_x(intent.direction.x);
    input->mutable_direction()->set_y(intent.direction.y);
    input->set_client_tick(static_cast<std::uint32_t>(world_.server_tick()));
    if (connection_->Send(move).has_value()) {
        ++report_.inputs_sent;
    }
    if (intent.cast) {
        game::v1::ClientMessage cast;
        auto* skill = cast.mutable_cast_skill();
        const common::RequestId request{++next_request_id_};
        skill->set_request_id(request.value());
        skill->set_skill_id(intent.cast->skill.value());
        if (const auto* entity = std::get_if<common::EntityId>(&intent.cast->target)) {
            skill->set_target_entity_id(entity->value());
        } else {
            const common::Vec2 direction = std::get<common::Vec2>(intent.cast->target);
            skill->mutable_direction()->set_x(direction.x);
            skill->mutable_direction()->set_y(direction.y);
        }
        world_.ExpectResult(request, RequestKind::kCast);
        if (connection_->Send(cast).has_value()) {
            ++report_.casts_sent;
        }
    }
    if (intent.pickup) {
        game::v1::ClientMessage pickup;
        auto* item = pickup.mutable_pickup_item();
        const common::RequestId request{++next_request_id_};
        item->set_request_id(request.value());
        item->set_item_entity_id(intent.pickup->value());
        world_.ExpectResult(request, RequestKind::kPickup);
        if (connection_->Send(pickup).has_value()) {
            ++report_.pickups_sent;
        }
    }
    if (intent.change_zone) {
        SendChangeZone(*intent.change_zone);
    }
    if (trade_step_) {
        if (trade_step_->resend_at && std::chrono::steady_clock::now() >= *trade_step_->resend_at) {
            SendHeldTradeStep(*trade_step_);
        }
    } else if (intent.trade) {
        SendTradeStep(*intent.trade);
    }
}

void BotSession::SendTradeStep(const bot::TradeIntent& step) {
    const common::RequestId request{++next_request_id_};
    game::v1::ClientMessage message;
    switch (step.step) {
        case bot::TradeStep::kOpen:
            message.mutable_trade_open()->set_request_id(request.value());
            message.mutable_trade_open()->set_target_entity_id(step.target.value());
            break;
        case bot::TradeStep::kOffer:
            message.mutable_trade_offer()->set_request_id(request.value());
            message.mutable_trade_offer()->set_trade_id(step.trade.ToWire());
            message.mutable_trade_offer()->set_gold(step.gold);
            break;
        case bot::TradeStep::kAccept:
            message.mutable_trade_accept()->set_request_id(request.value());
            message.mutable_trade_accept()->set_trade_id(step.trade.ToWire());
            message.mutable_trade_accept()->set_version(step.version);
            break;
        case bot::TradeStep::kCancel:
            message.mutable_trade_cancel()->set_request_id(request.value());
            message.mutable_trade_cancel()->set_trade_id(step.trade.ToWire());
            break;
    }
    SendHeldTradeStep(trade_step_.emplace(
        HeldTradeStep{.request = request, .message = std::move(message), .resend_at = {}}));
}

void BotSession::SendHeldTradeStep(HeldTradeStep& step) {
    step.resend_at.reset();
    world_.ExpectResult(step.request, RequestKind::kTrade);
    if (connection_->Send(step.message).has_value()) {
        ++report_.trade_steps_sent;
    }
}

void BotSession::SendChangeZone(common::PortalId portal) {
    if (!departure_) {
        departure_ = common::RequestId{++next_request_id_};
    }
    game::v1::ClientMessage change;
    change.mutable_change_zone()->set_request_id(departure_->value());
    change.mutable_change_zone()->set_portal_id(portal.value());
    world_.ExpectResult(*departure_, RequestKind::kChangeZone);
    if (connection_->Send(change).has_value()) {
        ++report_.transfers_sent;
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
    ping.mutable_ping()->set_client_time_ms(common::ToUnixMillis(common::WallNow()));
    ping_sent_at_ = std::chrono::steady_clock::now();
    // A closing connection drops the ping; no pong is expected from it then.
    std::ignore = connection_->Send(ping);
}

}  // namespace sa::loadgen
