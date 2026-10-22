#include "zone_link.hpp"

#include "sa/common/framed_connection.hpp"
#include "sa/common/framing.hpp"
#include "sa/common/testing/loopback.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace sa::gateway {
namespace {

using common::testing::RunUntil;

constexpr common::ZoneId kZone{1};
constexpr std::uint32_t kOtherZone = 2;
constexpr std::chrono::milliseconds kTestBackoff{10};
constexpr std::chrono::milliseconds kTestLinger{50};
constexpr std::chrono::milliseconds kTestEstablish{50};
constexpr std::chrono::milliseconds kTestHeartbeat{10};
constexpr std::chrono::milliseconds kTestSilence{50};

// Accepts gateway links. Each LinkHello is answered with a LinkWelcome naming `welcome_zone` unless
// `silent`, and each LinkPing with a LinkPong while `answer_pings`.
class FakeZone {
public:
    explicit FakeZone(asio::io_context& io)
        : acceptor_(io, {asio::ip::make_address("127.0.0.1"), 0}) {
        Accept();
    }

    [[nodiscard]] std::uint16_t port() const { return acceptor_.local_endpoint().port(); }

    std::uint32_t welcome_zone = kZone.value();
    bool silent = false;       // accept and say nothing, like a zone stuck before its welcome
    bool answer_pings = true;  // false: a zone host that vanished after the welcome
    int connections = 0;
    int pings = 0;

private:
    void Accept() {
        acceptor_.async_accept([this](const std::error_code& ec, asio::ip::tcp::socket socket) {
            if (ec) {
                return;
            }
            ++connections;
            auto connection = std::make_shared<common::FramedConnection>(
                std::move(socket), common::FrameLimits{}, kTestLinger);
            open_.push_back(connection);
            connection->Start(common::MessageConnection::Callbacks{
                .on_message =
                    [this, weak = std::weak_ptr{connection}](std::span<const std::byte> payload) {
                        if (auto c = weak.lock()) {
                            OnFrame(*c, payload);
                        }
                    },
                .on_closed = [](const std::error_code& /*reason*/) {},
            });
            Accept();
        });
    }

    void OnFrame(common::FramedConnection& connection, std::span<const std::byte> payload) {
        internal::v1::GatewayToZone message;
        if (!message.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
            throw std::logic_error{"the link sent an unparseable frame"};
        }
        internal::v1::ZoneToGateway reply;
        if (message.has_link_hello() && !silent) {
            reply.mutable_link_welcome()->set_zone_id(welcome_zone);
        } else if (message.has_link_ping()) {
            ++pings;
            if (!answer_pings) {
                return;
            }
            reply.mutable_link_pong()->set_nonce(message.link_ping().nonce());
        } else {
            return;
        }
        std::ignore = connection.Send(reply);
    }

    asio::ip::tcp::acceptor acceptor_;
    std::vector<std::shared_ptr<common::FramedConnection>> open_;
};

struct RecordingHandler final : ZoneLink::Handler {
    int ready = 0;
    int down = 0;
    void OnZoneReady(common::ZoneId /*zone*/,
                     const internal::v1::LinkWelcome& /*welcome*/) override {
        ++ready;
    }
    void OnZoneDown(common::ZoneId /*zone*/) override { ++down; }
    void OnSessionAttached(common::ZoneId /*zone*/,
                           const internal::v1::SessionAttached& /*attached*/) override {}
    void OnSessionRejected(common::ZoneId /*zone*/,
                           const internal::v1::SessionRejected& /*rejected*/) override {}
    void OnSessionEvicted(common::ZoneId /*zone*/,
                          const internal::v1::SessionEvicted& /*evicted*/) override {}
    void OnSessionTransfer(common::ZoneId /*zone*/,
                           const internal::v1::SessionTransfer& /*transfer*/) override {}
    void OnSessionOutput(common::ZoneId /*zone*/,
                         const internal::v1::SessionOutput& /*output*/) override {}
};

// Stops the link and runs until its connection has finished closing (the fake zone never closes,
// so the linger ends it). Until then the connection's callbacks hold the link.
void StopAndDrain(asio::io_context& io, std::shared_ptr<ZoneLink>& link) {
    link->Stop();
    RunUntil(io, [&] { return link.use_count() == 1; });
}

std::shared_ptr<ZoneLink> LinkTo(asio::io_context& io, const FakeZone& zone,
                                 RecordingHandler& handler) {
    return std::make_shared<ZoneLink>(io,
                                      ZoneLinkConfig{.zone = kZone,
                                                     .address = "127.0.0.1",
                                                     .port = zone.port(),
                                                     .frame_limits = common::FrameLimits{},
                                                     .reconnect_backoff = kTestBackoff,
                                                     .close_linger = kTestLinger,
                                                     .establish_timeout = kTestEstablish,
                                                     .heartbeat_interval = kTestHeartbeat,
                                                     .silence_timeout = kTestSilence},
                                      common::InstanceId::Generate(), handler);
}

TEST(ZoneLink, BecomesReadyWhenItsZoneWelcomesIt) {
    asio::io_context io;
    const FakeZone zone{io};
    RecordingHandler handler;
    auto link = LinkTo(io, zone, handler);
    link->Start();
    RunUntil(io, [&] { return handler.ready > 0; });
    EXPECT_TRUE(link->is_ready());
    StopAndDrain(io, link);
}

// A zone process answering for another zone id is a misconfiguration. The link aborts from
// inside its frame handler (the path that once freed the connection under its own read loop)
// and keeps retrying without ever becoming ready.
TEST(ZoneLink, AWelcomeFromAnotherZoneIsRefusedAndTheLinkRetries) {
    asio::io_context io;
    FakeZone zone{io};
    zone.welcome_zone = kOtherZone;
    RecordingHandler handler;
    auto link = LinkTo(io, zone, handler);
    link->Start();
    RunUntil(io, [&] { return zone.connections >= 2; });
    EXPECT_EQ(handler.ready, 0);
    EXPECT_FALSE(link->is_ready());
    StopAndDrain(io, link);
}

// A zone that accepts the connection but never welcomes it must not hold the link: the
// establishment deadline closes the attempt and the link tries again.
TEST(ZoneLink, AZoneThatNeverWelcomesIsGivenUpOnAndTriedAgain) {
    asio::io_context io;
    FakeZone zone{io};
    zone.silent = true;
    RecordingHandler handler;
    auto link = LinkTo(io, zone, handler);
    link->Start();
    RunUntil(io, [&] { return zone.connections >= 2; });
    EXPECT_EQ(handler.ready, 0);
    EXPECT_FALSE(link->is_ready());
    StopAndDrain(io, link);
}

// A zone that keeps answering is never mistaken for a dead one, however long the link idles.
TEST(ZoneLink, AZoneThatAnswersPingsKeepsTheLinkThroughManySilenceWindows) {
    asio::io_context io;
    const FakeZone zone{io};
    RecordingHandler handler;
    auto link = LinkTo(io, zone, handler);
    link->Start();
    RunUntil(io, [&] { return handler.ready > 0; });
    constexpr int kWindows = 4;
    io.restart();
    io.run_for(kTestSilence * kWindows);
    EXPECT_GE(zone.pings, kWindows) << "pings at the heartbeat interval";
    EXPECT_EQ(handler.down, 0);
    EXPECT_TRUE(link->is_ready());
    StopAndDrain(io, link);
}

// A zone host that vanished after the welcome sends nothing more. The link gives up after the
// silence timeout, which drops the zone's sessions (OnZoneDown), and connects again.
TEST(ZoneLink, AZoneThatFallsSilentIsDroppedAndConnectedAgain) {
    asio::io_context io;
    FakeZone zone{io};
    zone.answer_pings = false;
    RecordingHandler handler;
    auto link = LinkTo(io, zone, handler);
    link->Start();
    RunUntil(io, [&] { return handler.ready > 0; });
    RunUntil(io, [&] { return handler.down > 0 && zone.connections >= 2; });
    EXPECT_GT(zone.pings, 0);
    StopAndDrain(io, link);
}

}  // namespace
}  // namespace sa::gateway
