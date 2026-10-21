#include "zone_link.hpp"

#include "sa/common/bytes.hpp"
#include "sa/common/framing.hpp"
#include "sa/common/testing/loopback.hpp"

#include <asio/write.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sa::gateway {
namespace {

using common::testing::RunUntil;

constexpr common::ZoneId kZone{1};
constexpr std::uint32_t kOtherZone = 2;
constexpr std::chrono::milliseconds kTestBackoff{10};
constexpr std::chrono::milliseconds kTestLinger{50};
constexpr std::chrono::milliseconds kTestEstablish{50};

// Accepts gateway links and answers each with a LinkWelcome naming `welcome_zone`, unless `silent`.
class FakeZone {
public:
    explicit FakeZone(asio::io_context& io)
        : acceptor_(io, {asio::ip::make_address("127.0.0.1"), 0}) {
        Accept();
    }

    [[nodiscard]] std::uint16_t port() const { return acceptor_.local_endpoint().port(); }

    std::uint32_t welcome_zone = kZone.value();
    bool silent = false;  // accept and say nothing, like a zone stuck before its welcome
    int connections = 0;

private:
    void Accept() {
        acceptor_.async_accept([this](const std::error_code& ec, asio::ip::tcp::socket socket) {
            if (ec) {
                return;
            }
            ++connections;
            if (silent) {
                open_.push_back(std::move(socket));
                Accept();
                return;
            }
            internal::v1::ZoneToGateway welcome;
            welcome.mutable_link_welcome()->set_zone_id(welcome_zone);
            const std::string payload = welcome.SerializeAsString();
            std::vector<std::byte> frame;
            if (!common::EncodeFrame(common::AsBytes(payload), common::FrameLimits{}, frame)) {
                throw std::logic_error{"welcome does not encode"};
            }
            asio::write(socket, asio::buffer(frame));
            open_.push_back(std::move(socket));
            Accept();
        });
    }

    asio::ip::tcp::acceptor acceptor_;
    std::vector<asio::ip::tcp::socket> open_;
};

struct RecordingHandler final : ZoneLink::Handler {
    int ready = 0;
    int down = 0;
    void OnZoneReady(common::ZoneId /*zone*/,
                     const internal::v1::LinkWelcome& /*welcome*/) override {
        ++ready;
    }
    void OnZoneDown(common::ZoneId /*zone*/) override { ++down; }
    void OnSessionAttached(const internal::v1::SessionAttached& /*attached*/) override {}
    void OnSessionRejected(const internal::v1::SessionRejected& /*rejected*/) override {}
    void OnSessionEvicted(const internal::v1::SessionEvicted& /*evicted*/) override {}
    void OnSessionOutput(const internal::v1::SessionOutput& /*output*/) override {}
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
                                                     .establish_timeout = kTestEstablish},
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

}  // namespace
}  // namespace sa::gateway
