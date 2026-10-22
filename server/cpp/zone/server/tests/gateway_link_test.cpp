#include "gateway_link.hpp"

#include "sa/common/bytes.hpp"
#include "sa/common/framing.hpp"
#include "sa/common/instance_id.hpp"
#include "sa/common/testing/loopback.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sa::zone {
namespace {

using common::testing::Loopback;
using common::testing::RunUntil;

constexpr std::chrono::milliseconds kTestLinger{50};
constexpr std::chrono::milliseconds kTestSilence{50};

struct RecordingHandler final : GatewayLink::Handler {
    int ready = 0;
    int closed = 0;
    void OnLinkReady(GatewayLink& /*link*/) override { ++ready; }
    void OnAttach(GatewayLink& /*link*/, const internal::v1::SessionAttach& /*attach*/) override {}
    void OnDetach(GatewayLink& /*link*/, const internal::v1::SessionDetach& /*detach*/) override {}
    void OnInput(GatewayLink& /*link*/, const internal::v1::SessionInput& /*input*/) override {}
    void OnLinkClosed(GatewayLink& /*link*/) override { ++closed; }
};

// One GatewayToZone frame as the gateway writes it.
std::vector<std::byte> Frame(const internal::v1::GatewayToZone& message) {
    const std::string payload = message.SerializeAsString();
    std::vector<std::byte> frame;
    if (!common::EncodeFrame(common::AsBytes(payload), common::FrameLimits{}, frame)) {
        throw std::logic_error{"frame does not encode"};
    }
    return frame;
}

std::vector<std::byte> Hello() {
    internal::v1::GatewayToZone hello;
    hello.mutable_link_hello()->set_gateway_instance_id(common::InstanceId::Generate().ToWire());
    return Frame(hello);
}

std::vector<std::byte> Ping(std::uint64_t nonce) {
    internal::v1::GatewayToZone ping;
    ping.mutable_link_ping()->set_nonce(nonce);
    return Frame(ping);
}

std::shared_ptr<GatewayLink> Accept(Loopback& loop, RecordingHandler& handler) {
    auto link = std::make_shared<GatewayLink>(loop.TakeServerSocket(), common::FrameLimits{},
                                              kTestLinger, kTestSilence, handler);
    link->Start();
    return link;
}

// A gateway that vanished after its hello sends nothing more; its players must not stay in the
// world until TCP gives up, so the zone closes the link and the host drops its sessions.
TEST(GatewayLink, AGatewayThatFallsSilentIsClosed) {
    Loopback loop;
    RecordingHandler handler;
    auto link = Accept(loop, handler);
    loop.WriteFromPeer(Hello());
    RunUntil(loop.io(), [&] { return handler.ready == 1; });
    RunUntil(loop.io(), [&] { return handler.closed == 1; });
}

// A gateway that keeps pinging is never mistaken for a dead one, however long it carries no
// session traffic.
TEST(GatewayLink, AGatewayThatKeepsPingingStaysLinked) {
    Loopback loop;
    RecordingHandler handler;
    auto link = Accept(loop, handler);
    loop.WriteFromPeer(Hello());
    constexpr int kPings = 8;  // four silence windows at half the timeout apart
    for (int i = 0; i < kPings; ++i) {
        loop.io().restart();
        loop.io().run_for(kTestSilence / 2);
        loop.WriteFromPeer(Ping(static_cast<std::uint64_t>(i)));
    }
    loop.io().restart();
    loop.io().run_for(kTestSilence / 4);
    EXPECT_EQ(handler.ready, 1);
    EXPECT_EQ(handler.closed, 0);
    link->Close();
    RunUntil(loop.io(), [&] { return link.use_count() == 1; });
}

}  // namespace
}  // namespace sa::zone
