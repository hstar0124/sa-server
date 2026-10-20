#include "client_session.hpp"

#include "sa/common/testing/loopback.hpp"

#include "directory_client.hpp"
#include "listener.hpp"
#include "session_registry.hpp"
#include "test_tickets.hpp"

#include <asio/connect.hpp>
#include <asio/post.hpp>
#include <grpcpp/create_channel.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace sa::gateway {
namespace {

using common::testing::RunUntil;

constexpr std::chrono::milliseconds kTestHelloTimeout{20};
constexpr std::chrono::milliseconds kTestRpcTimeout{100};
constexpr std::uint32_t kTestRate = 10;

// Records what the session sends. Like a real transport it reports its close later, on the
// io_context, so the session finishes the way it does in production.
class FakeConnection final : public common::MessageConnection {
public:
    explicit FakeConnection(asio::io_context& io) : io_(io) {}

    void Start(Callbacks callbacks) override { callbacks_ = std::move(callbacks); }
    bool Send(const google::protobuf::MessageLite& message) override {
        if (closed_) {
            return false;
        }
        game::v1::ServerMessage sent;
        if (!sent.ParseFromString(message.SerializeAsString())) {
            throw std::logic_error{"the session sent something that is not a ServerMessage"};
        }
        sent_.push_back(std::move(sent));
        return true;
    }
    void Close() override { Finish(); }
    void Abort() override { Finish(); }
    [[nodiscard]] bool is_open() const override { return !closed_; }
    [[nodiscard]] std::string PeerAddress() const override { return "fake"; }

    [[nodiscard]] bool closed() const noexcept { return closed_; }
    [[nodiscard]] const std::vector<game::v1::ServerMessage>& sent() const noexcept {
        return sent_;
    }

private:
    void Finish() {
        if (std::exchange(closed_, true)) {
            return;
        }
        asio::post(io_, [on_closed = std::move(callbacks_.on_closed)] {
            if (on_closed) {
                on_closed({});
            }
        });
        callbacks_ = {};
    }

    asio::io_context& io_;
    Callbacks callbacks_;
    std::vector<game::v1::ServerMessage> sent_;
    bool closed_ = false;
};

// Sessions with real collaborators. The directory points at a port nothing listens on; these
// tests never get far enough to call it.
class ClientSessionTest : public ::testing::Test {
protected:
    asio::io_context io;
    EntryTicketVerifier verifier = test::Verifier();
    DirectoryClient directory{
        io, grpc::CreateChannel("127.0.0.1:1", grpc::InsecureChannelCredentials()), kTestRpcTimeout,
        common::InstanceId::Generate()};
    ZoneRouter zones;
    SessionRegistry registry;

    [[nodiscard]] ClientSession::Dependencies Deps() {
        return ClientSession::Dependencies{
            .io = io,
            .verifier = verifier,
            .protocol = {.max_messages_per_second = kTestRate, .hello_timeout = kTestHelloTimeout},
            .directory = directory,
            .zones = zones,
            .registry = registry};
    }

    struct Opened {
        std::shared_ptr<FakeConnection> connection;
        std::shared_ptr<ClientSession> session;
    };

    [[nodiscard]] Opened Open() {
        auto connection = std::make_shared<FakeConnection>(io);
        auto session = std::make_shared<ClientSession>(connection, Deps());
        session->Start();
        return {.connection = connection, .session = session};
    }
};

game::v1::KickReason KickIn(const FakeConnection& connection) {
    for (const game::v1::ServerMessage& message : connection.sent()) {
        if (message.has_kick()) {
            return message.kick().reason();
        }
    }
    return game::v1::KICK_REASON_UNSPECIFIED;
}

TEST_F(ClientSessionTest, AConnectionThatNeverSaysHelloIsKickedAtTheDeadline) {
    const auto [connection, session] = Open();
    RunUntil(io, [&] { return registry.size() == 0; });  // kicked, closed and finished
    EXPECT_EQ(KickIn(*connection), game::v1::KICK_REASON_PROTOCOL_ERROR);
}

TEST_F(ClientSessionTest, ClosingAllKicksEverySessionWithTheGivenReason) {
    const auto first = Open();
    const auto second = Open();
    registry.CloseAll(game::v1::KICK_REASON_SERVICE_UNAVAILABLE);

    for (const auto& opened : {first, second}) {
        EXPECT_TRUE(opened.connection->closed());
        EXPECT_EQ(KickIn(*opened.connection), game::v1::KICK_REASON_SERVICE_UNAVAILABLE);
    }
    RunUntil(io, [&] { return registry.size() == 0; });
}

// The listener refuses connections beyond its ceiling; a refused peer sees its socket closed.
TEST_F(ClientSessionTest, TheListenerRefusesConnectionsBeyondItsCeiling) {
    Listener listener{
        io, ListenerConfig{.name = "test", .address = "127.0.0.1", .port = 0, .max_connections = 1},
        [this](asio::ip::tcp::socket socket) {
            socket.close();  // the session only needs to exist, not to talk
            return std::make_shared<ClientSession>(std::make_shared<FakeConnection>(io), Deps());
        }};
    listener.Start();
    const asio::ip::tcp::endpoint endpoint{asio::ip::make_address("127.0.0.1"),
                                           listener.local_port()};
    asio::ip::tcp::socket admitted{io};
    asio::ip::tcp::socket refused{io};
    admitted.connect(endpoint);
    refused.connect(endpoint);
    RunUntil(io, [&] { return listener.open_sessions() == 1 && refused.available() == 0; });

    refused.non_blocking(true);
    std::array<char, 1> byte{};
    std::error_code ec;
    RunUntil(io, [&] {
        (void)refused.read_some(asio::buffer(byte), ec);
        return ec != asio::error::would_block;
    });
    EXPECT_EQ(ec, asio::error::eof) << "the second connection was closed by the listener";
    EXPECT_EQ(listener.open_sessions(), 1U);
    listener.Stop();
    RunUntil(io, [&] { return listener.open_sessions() == 0; });
}

}  // namespace
}  // namespace sa::gateway
