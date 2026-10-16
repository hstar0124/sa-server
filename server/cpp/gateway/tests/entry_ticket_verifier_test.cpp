#include "entry_ticket_verifier.hpp"

#include "test_tickets.hpp"

#include <gtest/gtest.h>

namespace sa::gateway {
namespace {

std::span<const std::byte> Bytes(const std::string& s) {
    return std::as_bytes(std::span{s});
}

TEST(EntryTicketVerifier, AcceptsAWellSignedTicketAndExposesItsIdentity) {
    const auto verifier = test::Verifier();
    const auto id = common::InstanceId::Generate();
    const std::string wire =
        test::SignedTicket(test::TicketFields{.character = 77}, test::Key(), id);

    const auto result = verifier.Verify(Bytes(wire), test::kNow);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ticket_id, id);
    EXPECT_EQ(result->character, common::CharacterId{77});
    EXPECT_EQ(result->account, common::AccountId{10});
    EXPECT_EQ(result->world, common::WorldId{1});
}

TEST(EntryTicketVerifier, RejectsTamperedPayload) {
    const auto verifier = test::Verifier();
    const std::string wire = test::SignedTicket(test::TicketFields{});
    backend::v1::SignedEntryTicket envelope;
    ASSERT_TRUE(envelope.ParseFromString(wire));
    backend::v1::EntryTicket ticket;
    ASSERT_TRUE(ticket.ParseFromString(envelope.ticket()));
    ticket.set_character_id(ticket.character_id() + 1);  // claim someone else's character
    envelope.set_ticket(ticket.SerializeAsString());

    EXPECT_EQ(verifier.Verify(Bytes(envelope.SerializeAsString()), test::kNow).error(),
              TicketError::kBadSignature);
}

TEST(EntryTicketVerifier, RejectsWrongKeyAndUnknownKeyId) {
    const auto verifier = test::Verifier();
    const std::vector<std::byte> other_key(32, std::byte{0x11});
    EXPECT_EQ(
        verifier.Verify(Bytes(test::SignedTicket(test::TicketFields{}, other_key)), test::kNow)
            .error(),
        TicketError::kBadSignature);
    EXPECT_EQ(
        verifier.Verify(Bytes(test::SignedTicket(test::TicketFields{.key_id = 9})), test::kNow)
            .error(),
        TicketError::kUnknownKey);
}

TEST(EntryTicketVerifier, EnforcesTheValidityWindowWithSkewTolerance) {
    const auto verifier = test::Verifier();
    const std::string wire = test::SignedTicket(test::TicketFields{});

    EXPECT_TRUE(verifier.Verify(Bytes(wire), test::kNow + 30'000 + test::kSkewMs - 1).has_value());
    EXPECT_EQ(verifier.Verify(Bytes(wire), test::kNow + 30'000 + test::kSkewMs).error(),
              TicketError::kExpired);
    EXPECT_TRUE(verifier.Verify(Bytes(wire), test::kNow - 1'000 - test::kSkewMs).has_value());
    EXPECT_EQ(verifier.Verify(Bytes(wire), test::kNow - 1'000 - test::kSkewMs - 1).error(),
              TicketError::kNotYetValid);
}

TEST(EntryTicketVerifier, RejectsGarbage) {
    const auto verifier = test::Verifier();
    EXPECT_EQ(verifier.Verify(Bytes("not a ticket"), test::kNow).error(), TicketError::kMalformed);
    EXPECT_EQ(verifier.Verify({}, test::kNow).error(), TicketError::kMalformed);
}

}  // namespace
}  // namespace sa::gateway
