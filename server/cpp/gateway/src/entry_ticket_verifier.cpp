#include "entry_ticket_verifier.hpp"

#include "sa/common/hmac.hpp"

#include <backend/entry_ticket.pb.h>

namespace sa::gateway {

EntryTicketVerifier::EntryTicketVerifier(std::vector<Key> keys, std::uint64_t max_clock_skew_ms)
    : max_clock_skew_ms_(max_clock_skew_ms) {
    for (Key& key : keys) {
        keys_.insert_or_assign(key.id, std::move(key.secret));
    }
}

std::expected<VerifiedTicket, TicketError> EntryTicketVerifier::Verify(
    std::span<const std::byte> signed_ticket, std::uint64_t now_ms) const {
    backend::v1::SignedEntryTicket envelope;
    if (!envelope.ParseFromArray(signed_ticket.data(), static_cast<int>(signed_ticket.size()))) {
        return std::unexpected(TicketError::kMalformed);
    }
    backend::v1::EntryTicket ticket;
    if (!ticket.ParseFromString(envelope.ticket())) {
        return std::unexpected(TicketError::kMalformed);
    }
    const auto ticket_id = common::InstanceId::FromString(ticket.ticket_id());
    if (!ticket_id) {
        return std::unexpected(TicketError::kMalformed);
    }

    const auto key = keys_.find(ticket.key_id());
    if (key == keys_.end()) {
        return std::unexpected(TicketError::kUnknownKey);
    }
    const auto expected =
        common::HmacSha256(key->second, std::as_bytes(std::span{envelope.ticket()}));
    if (!common::ConstantTimeEquals(expected, std::as_bytes(std::span{envelope.signature()}))) {
        return std::unexpected(TicketError::kBadSignature);
    }

    // Issued by another machine's clock: tolerate skew in both directions.
    if (ticket.issued_at_ms() > now_ms + max_clock_skew_ms_) {
        return std::unexpected(TicketError::kNotYetValid);
    }
    if (ticket.expires_at_ms() + max_clock_skew_ms_ <= now_ms) {
        return std::unexpected(TicketError::kExpired);
    }

    return VerifiedTicket{.ticket_id = *ticket_id,
                          .account = common::AccountId{ticket.account_id()},
                          .character = common::CharacterId{ticket.character_id()},
                          .world = common::WorldId{ticket.world_id()},
                          .expires_at_ms = ticket.expires_at_ms()};
}

}  // namespace sa::gateway
