#include "session_protocol.hpp"

#include "sa/common/protocol_limits.hpp"

#include "test_tickets.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <variant>

namespace sa::gateway {
namespace {

using game::v1::ClientMessage;
using game::v1::ServerMessage;
using State = SessionProtocol::State;

constexpr SessionProtocolConfig kConfig{.max_messages_per_second = 10};
constexpr common::ZoneId kZone1{1};
constexpr common::ZoneId kZone2{2};

ClientMessage PingMessage(std::uint32_t nonce) {
    ClientMessage m;
    m.mutable_ping()->set_nonce(nonce);
    m.mutable_ping()->set_client_time_ms(123);
    return m;
}

ClientMessage HelloMessage(std::string ticket,
                           game::v1::ProtocolVersion version = common::protocol::kCurrentVersion) {
    ClientMessage m;
    m.mutable_hello()->set_protocol_version(version);
    m.mutable_hello()->set_entry_ticket(std::move(ticket));
    return m;
}

ClientMessage MoveMessage(std::uint32_t seq) {
    ClientMessage m;
    m.mutable_move_input()->set_input_seq(seq);
    return m;
}

backend::v1::RedeemEntryTicketResponse Granted(std::uint32_t zone = 1, std::uint64_t epoch = 1) {
    backend::v1::RedeemEntryTicketResponse r;
    r.set_outcome(backend::v1::REDEEM_OUTCOME_GRANTED);
    r.mutable_zone()->set_zone_id(zone);
    r.set_owner_epoch(epoch);
    r.mutable_character()->set_character_id(20);
    r.mutable_character()->set_name("Hero");
    r.mutable_character()->set_class_id(2);
    return r;
}

using namespace std::chrono_literals;

// A message arriving `offset` after the test's reference instant, on both clocks. The monotonic
// origin is well past zero, so the first message always opens a fresh rate window.
ArrivalTime At(std::chrono::milliseconds offset = {}) {
    constexpr std::chrono::hours kMonotonicOrigin{1};
    return ArrivalTime{
        .monotonic = std::chrono::steady_clock::time_point{kMonotonicOrigin + offset},
        .wall = test::kNow + offset};
}

const ServerMessage& OnlyReply(const SessionProtocol::Reaction& r) {
    if (r.replies.size() != 1) {
        throw std::logic_error{"expected exactly one reply"};
    }
    return r.replies.front();
}

template <typename E>
const E& OnlyEffect(const SessionProtocol::Reaction& r) {
    if (r.effects.size() != 1 || !std::holds_alternative<E>(r.effects.front())) {
        throw std::logic_error{"expected exactly one effect of the requested type"};
    }
    return std::get<E>(r.effects.front());
}

template <typename E>
bool HasEffect(const SessionProtocol::Reaction& r) {
    return std::ranges::any_of(r.effects,
                               [](const auto& e) { return std::holds_alternative<E>(e); });
}

internal::v1::SessionTransfer TransferTo(common::ZoneId zone, std::uint64_t epoch) {
    internal::v1::SessionTransfer t;
    t.set_to_zone_id(zone.value());
    t.set_to_zone_instance_id(common::InstanceId::Generate().ToWire());
    t.set_owner_epoch(epoch);
    t.mutable_arrival_position()->set_x(8.0F);
    t.mutable_arrival_position()->set_y(100.0F);
    return t;
}

class SessionProtocolTest : public ::testing::Test {
protected:
    EntryTicketVerifier verifier = test::Verifier();
    SessionProtocol protocol{verifier, kConfig, common::InstanceId::Generate()};

    // Drives a fresh protocol to the zone attach and returns that attach.
    SessionProtocol::AttachToZone FirstAttach(const backend::v1::RedeemEntryTicketResponse& grant) {
        SessionProtocol fresh{verifier, kConfig, common::InstanceId::Generate()};
        (void)fresh.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
        return OnlyEffect<SessionProtocol::AttachToZone>(fresh.OnRedeemResult(grant));
    }

    // Drives the happy path up to the zone attach and returns the attach effect.
    SessionProtocol::AttachToZone EnterZone() {
        const auto hello = protocol.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
        EXPECT_EQ(protocol.state(), State::kRedeeming);
        EXPECT_TRUE(HasEffect<SessionProtocol::RedeemTicket>(hello));
        const auto redeem = protocol.OnRedeemResult(Granted());
        EXPECT_EQ(protocol.state(), State::kAttaching);
        const auto attach = OnlyEffect<SessionProtocol::AttachToZone>(redeem);
        EXPECT_TRUE(protocol.OnZoneAttached(kZone1, common::EntityId{5}).effects.empty());
        EXPECT_EQ(protocol.state(), State::kInZone);
        return attach;
    }
};

TEST_F(SessionProtocolTest, PingIsAnsweredInAnyOpenState) {
    const auto reaction = protocol.OnClientMessage(PingMessage(9), At());
    EXPECT_EQ(OnlyReply(reaction).pong().nonce(), 9U);
    EXPECT_EQ(OnlyReply(reaction).pong().server_time_ms(), common::ToUnixMillis(test::kNow));
    EXPECT_FALSE(reaction.close);
}

TEST_F(SessionProtocolTest, HappyPathCarriesVerifiedIdentityIntoTheAttach) {
    const auto attach = EnterZone();
    EXPECT_EQ(attach.zone, common::ZoneId{1});
    EXPECT_EQ(attach.attach.account_id(), 10U) << "from the signed ticket, not the response";
    EXPECT_EQ(attach.attach.character_id(), 20U);
    EXPECT_EQ(attach.attach.character_name(), "Hero");
    EXPECT_EQ(attach.attach.class_id(), 2U);
    EXPECT_EQ(attach.attach.owner_epoch(), 1U);
    EXPECT_EQ(attach.attach.gateway_session_id(), protocol.session_id().ToWire());
}

TEST_F(SessionProtocolTest, InZoneInputsAreForwardedAndOutputsPassThrough) {
    EnterZone();
    const auto forward = protocol.OnClientMessage(MoveMessage(3), At());
    EXPECT_EQ(OnlyEffect<SessionProtocol::ForwardToZone>(forward).message.move_input().input_seq(),
              3U);

    ServerMessage snapshot;
    snapshot.mutable_world_snapshot()->set_server_tick(42);
    EXPECT_EQ(OnlyReply(protocol.OnZoneOutput(kZone1, snapshot)).world_snapshot().server_tick(),
              42U);
}

TEST_F(SessionProtocolTest, DisconnectInZoneDetachesAndEndsTheSession) {
    EnterZone();
    const auto reaction = protocol.OnClientDisconnected();
    EXPECT_EQ(protocol.state(), State::kClosed);
    ASSERT_EQ(reaction.effects.size(), 2U);
    EXPECT_TRUE(HasEffect<SessionProtocol::DetachFromZone>(reaction));
    EXPECT_TRUE(HasEffect<SessionProtocol::EndSession>(reaction));
    EXPECT_TRUE(protocol.OnClientDisconnected().effects.empty()) << "idempotent";
}

TEST_F(SessionProtocolTest, InputBeforeHelloOrBeforeAttachIsAProtocolError) {
    auto reaction = protocol.OnClientMessage(MoveMessage(1), At());
    EXPECT_EQ(OnlyReply(reaction).kick().reason(), game::v1::KICK_REASON_PROTOCOL_ERROR);
    EXPECT_TRUE(reaction.close);
    EXPECT_EQ(protocol.state(), State::kClosed);
    EXPECT_FALSE(HasEffect<SessionProtocol::EndSession>(reaction)) << "nothing was granted";
}

TEST_F(SessionProtocolTest, BadTicketOrVersionIsKickedWithoutContactingTheBackend) {
    SessionProtocol bad_version{verifier, kConfig, common::InstanceId::Generate()};
    const auto v = bad_version.OnClientMessage(
        HelloMessage(test::SignedTicket({}), game::v1::PROTOCOL_VERSION_UNSPECIFIED), At());
    EXPECT_EQ(OnlyReply(v).kick().reason(), game::v1::KICK_REASON_PROTOCOL_VERSION);

    const auto t = protocol.OnClientMessage(HelloMessage("garbage"), At());
    EXPECT_EQ(OnlyReply(t).kick().reason(), game::v1::KICK_REASON_INVALID_TICKET);
    EXPECT_TRUE(t.effects.empty());
}

TEST_F(SessionProtocolTest, RedeemOutcomesMapToKicksAndNeverLeaveASessionDangling) {
    struct Case {
        backend::v1::RedeemOutcome outcome;
        game::v1::KickReason kick;
    };
    for (const Case c : {Case{.outcome = backend::v1::REDEEM_OUTCOME_ALREADY_REDEEMED,
                              .kick = game::v1::KICK_REASON_INVALID_TICKET},
                         Case{.outcome = backend::v1::REDEEM_OUTCOME_ACTIVE_SESSION_EXISTS,
                              .kick = game::v1::KICK_REASON_DUPLICATE_SESSION},
                         Case{.outcome = backend::v1::REDEEM_OUTCOME_NO_ZONE_AVAILABLE,
                              .kick = game::v1::KICK_REASON_ZONE_UNAVAILABLE},
                         Case{.outcome = backend::v1::REDEEM_OUTCOME_UNKNOWN_GATEWAY,
                              .kick = game::v1::KICK_REASON_SERVICE_UNAVAILABLE}}) {
        SessionProtocol p{verifier, kConfig, common::InstanceId::Generate()};
        (void)p.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
        backend::v1::RedeemEntryTicketResponse r;
        r.set_outcome(c.outcome);
        const auto reaction = p.OnRedeemResult(r);
        EXPECT_EQ(OnlyReply(reaction).kick().reason(), c.kick)
            << backend::v1::RedeemOutcome_Name(c.outcome);
        EXPECT_FALSE(HasEffect<SessionProtocol::EndSession>(reaction))
            << "no session row was created";
        EXPECT_EQ(p.state(), State::kClosed);
    }
}

TEST_F(SessionProtocolTest, ZoneRejectionAfterGrantEndsTheBackendSession) {
    (void)protocol.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
    (void)protocol.OnRedeemResult(Granted());
    const auto reaction = protocol.OnZoneRejected(kZone1, internal::v1::REJECT_REASON_ZONE_FULL);
    EXPECT_EQ(OnlyReply(reaction).kick().reason(), game::v1::KICK_REASON_ZONE_UNAVAILABLE);
    EXPECT_TRUE(HasEffect<SessionProtocol::EndSession>(reaction));
    EXPECT_FALSE(HasEffect<SessionProtocol::DetachFromZone>(reaction))
        << "the zone never admitted us";
}

TEST_F(SessionProtocolTest, EvictionAndLinkLossKickWithoutDetaching) {
    EnterZone();
    auto evicted = protocol.OnZoneEvicted(kZone1, internal::v1::EVICT_REASON_LEASE_LOST);
    EXPECT_EQ(OnlyReply(evicted).kick().reason(), game::v1::KICK_REASON_ZONE_UNAVAILABLE);
    EXPECT_FALSE(HasEffect<SessionProtocol::DetachFromZone>(evicted));
    EXPECT_TRUE(HasEffect<SessionProtocol::EndSession>(evicted));

    SessionProtocol other{verifier, kConfig, common::InstanceId::Generate()};
    (void)other.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
    (void)other.OnRedeemResult(Granted());
    (void)other.OnZoneAttached(kZone1, common::EntityId{1});
    const auto down = other.OnZoneLinkDown();
    EXPECT_EQ(OnlyReply(down).kick().reason(), game::v1::KICK_REASON_SERVICE_UNAVAILABLE);
    EXPECT_TRUE(HasEffect<SessionProtocol::EndSession>(down));
}

TEST_F(SessionProtocolTest, AGrantArrivingAfterTheClientLeftEndsTheBackendSession) {
    (void)protocol.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
    const auto left = protocol.OnClientDisconnected();
    EXPECT_FALSE(HasEffect<SessionProtocol::EndSession>(left)) << "nothing granted yet";

    const auto late = protocol.OnRedeemResult(Granted());
    EXPECT_TRUE(late.replies.empty());
    EXPECT_TRUE(HasEffect<SessionProtocol::EndSession>(late));
    EXPECT_FALSE(HasEffect<SessionProtocol::AttachToZone>(late));
    EXPECT_TRUE(protocol.OnRedeemResult(Granted()).effects.empty()) << "one answer per redeem";
    EXPECT_TRUE(protocol.OnZoneAttached(kZone1, common::EntityId{1}).effects.empty());
    EXPECT_TRUE(protocol.OnZoneOutput(kZone1, ServerMessage{}).replies.empty());
}

TEST_F(SessionProtocolTest, ARefusalArrivingAfterTheClientLeftOwesNothing) {
    (void)protocol.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
    (void)protocol.OnClientDisconnected();
    backend::v1::RedeemEntryTicketResponse refused;
    refused.set_outcome(backend::v1::REDEEM_OUTCOME_ACTIVE_SESSION_EXISTS);
    EXPECT_TRUE(protocol.OnRedeemResult(refused).effects.empty());
}

TEST_F(SessionProtocolTest, ARedeemWithAnUnknownOutcomeEndsTheSessionInCaseItWasGranted) {
    (void)protocol.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
    const auto failed = protocol.OnRedeemFailed();
    EXPECT_EQ(OnlyReply(failed).kick().reason(), game::v1::KICK_REASON_SERVICE_UNAVAILABLE);
    EXPECT_TRUE(HasEffect<SessionProtocol::EndSession>(failed));

    SessionProtocol left{verifier, kConfig, common::InstanceId::Generate()};
    (void)left.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
    (void)left.OnClientDisconnected();
    EXPECT_TRUE(HasEffect<SessionProtocol::EndSession>(left.OnRedeemFailed()));
}

TEST_F(SessionProtocolTest, NoHelloByTheDeadlineIsKickedAndAHelloInTimeIsNot) {
    const auto silent = protocol.OnHelloDeadline();
    EXPECT_EQ(OnlyReply(silent).kick().reason(), game::v1::KICK_REASON_PROTOCOL_ERROR);
    EXPECT_TRUE(silent.close);

    SessionProtocol greeted{verifier, kConfig, common::InstanceId::Generate()};
    (void)greeted.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
    EXPECT_FALSE(greeted.OnHelloDeadline().close);
}

TEST_F(SessionProtocolTest, AServerCloseKicksWithItsReasonDetachesAndEndsTheSession) {
    EnterZone();
    const auto closed = protocol.OnServerClose(game::v1::KICK_REASON_SERVICE_UNAVAILABLE);
    EXPECT_EQ(OnlyReply(closed).kick().reason(), game::v1::KICK_REASON_SERVICE_UNAVAILABLE);
    EXPECT_TRUE(closed.close);
    const auto detach = std::ranges::find_if(closed.effects, [](const auto& effect) {
        return std::holds_alternative<SessionProtocol::DetachFromZone>(effect);
    });
    ASSERT_NE(detach, closed.effects.end());
    EXPECT_EQ(std::get<SessionProtocol::DetachFromZone>(*detach).reason,
              internal::v1::DETACH_REASON_GATEWAY_SHUTDOWN);
    EXPECT_TRUE(HasEffect<SessionProtocol::EndSession>(closed));
    EXPECT_FALSE(protocol.OnServerClose(game::v1::KICK_REASON_SERVER_SHUTDOWN).close)
        << "closing twice does nothing";
}

TEST_F(SessionProtocolTest, FloodingIsRateLimited) {
    EnterZone();
    // One second after the Hello a new window starts: max_messages_per_second messages pass and
    // the next one is a flood.
    const ArrivalTime window = At(1'000ms);
    for (std::uint32_t seq = 1; seq <= kConfig.max_messages_per_second; ++seq) {
        EXPECT_FALSE(protocol.OnClientMessage(MoveMessage(seq), window).close) << seq;
    }
    const auto reaction =
        protocol.OnClientMessage(MoveMessage(kConfig.max_messages_per_second + 1), window);
    EXPECT_EQ(OnlyReply(reaction).kick().reason(), game::v1::KICK_REASON_RATE_LIMITED);
}

TEST_F(SessionProtocolTest, RateLimitWindowResets) {
    EnterZone();
    for (std::uint32_t i = 0; i < kConfig.max_messages_per_second; ++i) {
        (void)protocol.OnClientMessage(MoveMessage(i + 1), At(5'000ms));
    }
    EXPECT_FALSE(protocol.OnClientMessage(MoveMessage(99), At(6'000ms)).close) << "new window";
}

TEST_F(SessionProtocolTest, CheckpointInTheGrantBecomesTheAttachPosition) {
    backend::v1::RedeemEntryTicketResponse granted = Granted();
    granted.mutable_checkpoint()->set_zone_id(1);
    granted.mutable_checkpoint()->mutable_position()->set_x(11.0F);
    granted.mutable_checkpoint()->mutable_position()->set_y(21.0F);
    const auto attach = FirstAttach(granted);
    ASSERT_TRUE(attach.attach.has_position());
    EXPECT_FLOAT_EQ(attach.attach.position().x(), 11.0F);
    EXPECT_FLOAT_EQ(attach.attach.position().y(), 21.0F);
    EXPECT_FALSE(FirstAttach(Granted()).attach.has_position())
        << "without a checkpoint the zone picks a spawn";
}

TEST_F(SessionProtocolTest, TransferReattachesToTheDestinationUnderTheNewEpochAndDropsInputs) {
    EnterZone();
    const auto reaction = protocol.OnZoneTransfer(kZone1, TransferTo(kZone2, 2));
    EXPECT_EQ(protocol.state(), State::kTransferring);
    EXPECT_EQ(protocol.zone(), kZone2);
    const auto attach = OnlyEffect<SessionProtocol::AttachToZone>(reaction);
    EXPECT_EQ(attach.zone, kZone2);
    EXPECT_EQ(attach.attach.owner_epoch(), 2U);
    EXPECT_EQ(attach.attach.character_id(), 20U) << "same identity as the first attach";
    EXPECT_EQ(attach.attach.gateway_session_id(), protocol.session_id().ToWire());
    EXPECT_FLOAT_EQ(attach.attach.position().x(), 8.0F);
    EXPECT_FLOAT_EQ(attach.attach.position().y(), 100.0F);

    // The client keeps moving until it sees the new Welcome: no kick, no forward.
    const auto moving = protocol.OnClientMessage(MoveMessage(7), At());
    EXPECT_TRUE(moving.effects.empty());
    EXPECT_TRUE(moving.replies.empty());
    EXPECT_FALSE(moving.close);
    EXPECT_EQ(OnlyReply(protocol.OnClientMessage(PingMessage(1), At())).pong().nonce(), 1U);

    EXPECT_TRUE(protocol.OnZoneAttached(kZone2, common::EntityId{9}).effects.empty());
    EXPECT_EQ(protocol.state(), State::kInZone);
    EXPECT_TRUE(
        HasEffect<SessionProtocol::ForwardToZone>(protocol.OnClientMessage(MoveMessage(8), At())));
}

TEST_F(SessionProtocolTest, AfterATransferTheOriginNoLongerSpeaksForTheSession) {
    EnterZone();
    (void)protocol.OnZoneTransfer(kZone1, TransferTo(kZone2, 2));
    EXPECT_TRUE(protocol.OnZoneAttached(kZone1, common::EntityId{5}).effects.empty());
    EXPECT_EQ(protocol.state(), State::kTransferring) << "only the destination completes the move";
    (void)protocol.OnZoneAttached(kZone2, common::EntityId{9});

    ServerMessage late;
    late.mutable_world_snapshot()->set_server_tick(1);
    EXPECT_TRUE(protocol.OnZoneOutput(kZone1, late).replies.empty());
    const auto evicted = protocol.OnZoneEvicted(kZone1, internal::v1::EVICT_REASON_LEASE_LOST);
    EXPECT_TRUE(evicted.replies.empty());
    EXPECT_FALSE(evicted.close);
    EXPECT_TRUE(protocol.OnZoneTransfer(kZone1, TransferTo(kZone1, 3)).effects.empty());
    EXPECT_EQ(protocol.state(), State::kInZone);
    EXPECT_EQ(protocol.zone(), kZone2);
}

TEST_F(SessionProtocolTest, AZoneThatDoesNotHoldTheSessionIsIgnored) {
    (void)protocol.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
    (void)protocol.OnRedeemResult(Granted());
    EXPECT_TRUE(
        protocol.OnZoneRejected(kZone2, internal::v1::REJECT_REASON_ZONE_FULL).replies.empty());
    EXPECT_TRUE(protocol.OnZoneAttached(kZone2, common::EntityId{5}).effects.empty());
    EXPECT_EQ(protocol.state(), State::kAttaching);
}

TEST_F(SessionProtocolTest, StaleOrMalformedTransfersChangeNothing) {
    EnterZone();
    EXPECT_TRUE(protocol.OnZoneTransfer(kZone2, TransferTo(kZone2, 2)).effects.empty())
        << "only the zone that holds the session hands it on";
    EXPECT_TRUE(protocol.OnZoneTransfer(kZone1, TransferTo(kZone2, 1)).effects.empty())
        << "same generation";
    EXPECT_TRUE(protocol.OnZoneTransfer(kZone1, TransferTo(common::ZoneId{}, 2)).effects.empty())
        << "no destination";
    EXPECT_TRUE(protocol.OnZoneTransfer(kZone1, TransferTo(kZone1, 2)).effects.empty())
        << "to itself";
    EXPECT_EQ(protocol.state(), State::kInZone);
    EXPECT_EQ(protocol.zone(), kZone1);
}

TEST_F(SessionProtocolTest, DestinationRefusalOrDisconnectDuringTransferCleansUp) {
    EnterZone();
    (void)protocol.OnZoneTransfer(kZone1, TransferTo(kZone2, 2));
    const auto refused = protocol.OnZoneRejected(kZone2, internal::v1::REJECT_REASON_STALE_EPOCH);
    EXPECT_EQ(OnlyReply(refused).kick().reason(), game::v1::KICK_REASON_ZONE_UNAVAILABLE);
    EXPECT_TRUE(HasEffect<SessionProtocol::EndSession>(refused));
    EXPECT_EQ(protocol.state(), State::kClosed);

    SessionProtocol other{verifier, kConfig, common::InstanceId::Generate()};
    (void)other.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
    (void)other.OnRedeemResult(Granted());
    (void)other.OnZoneAttached(kZone1, common::EntityId{1});
    (void)other.OnZoneTransfer(kZone1, TransferTo(kZone2, 2));
    const auto gone = other.OnClientDisconnected();
    ASSERT_EQ(gone.effects.size(), 2U);
    const auto detach = std::ranges::find_if(gone.effects, [](const auto& effect) {
        return std::holds_alternative<SessionProtocol::DetachFromZone>(effect);
    });
    ASSERT_NE(detach, gone.effects.end());
    EXPECT_EQ(std::get<SessionProtocol::DetachFromZone>(*detach).zone, kZone2)
        << "the pending attach at the destination is withdrawn";
    EXPECT_TRUE(HasEffect<SessionProtocol::EndSession>(gone));
}

TEST_F(SessionProtocolTest, TransferIsIgnoredUnlessInZone) {
    EXPECT_TRUE(protocol.OnZoneTransfer(kZone1, TransferTo(kZone2, 2)).effects.empty());
    (void)protocol.OnClientMessage(HelloMessage(test::SignedTicket({})), At());
    (void)protocol.OnRedeemResult(Granted());
    EXPECT_TRUE(protocol.OnZoneTransfer(kZone1, TransferTo(kZone2, 2)).effects.empty())
        << "still attaching";
    EXPECT_EQ(protocol.state(), State::kAttaching);
}

}  // namespace
}  // namespace sa::gateway
