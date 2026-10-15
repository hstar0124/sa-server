#pragma once

#include "sa/common/bytes.hpp"
#include "sa/common/hmac.hpp"
#include "sa/common/instance_id.hpp"

#include "entry_ticket_verifier.hpp"

#include <backend/entry_ticket.pb.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Builds tickets exactly as the Backend's EntryTicketIssuer does, so the verifier is tested
// against the real wire format.
namespace sa::gateway::test {

inline const std::vector<std::byte>& Key() {
    static const std::vector<std::byte> key(32, std::byte{0x5a});
    return key;
}
constexpr std::uint32_t kKeyId = 1;
constexpr std::uint64_t kNow = 1'700'000'000'000;
constexpr std::uint64_t kSkewMs = 2'000;

struct TicketFields {
    std::uint64_t account = 10;
    std::uint64_t character = 20;
    std::uint64_t world = 1;
    std::uint64_t issued_at = kNow - 1'000;
    std::uint64_t expires_at = kNow + 30'000;
    std::uint32_t key_id = kKeyId;
};

inline std::string SignedTicket(const TicketFields& f, const std::vector<std::byte>& key = Key(),
                                common::InstanceId id = common::InstanceId::Generate()) {
    backend::v1::EntryTicket ticket;
    ticket.set_ticket_id(id.ToWire());
    ticket.set_account_id(f.account);
    ticket.set_character_id(f.character);
    ticket.set_world_id(f.world);
    ticket.set_issued_at_ms(f.issued_at);
    ticket.set_expires_at_ms(f.expires_at);
    ticket.set_key_id(f.key_id);
    backend::v1::SignedEntryTicket envelope;
    envelope.set_ticket(ticket.SerializeAsString());
    const auto digest = common::HmacSha256(key, std::as_bytes(std::span{envelope.ticket()}));
    envelope.set_signature(common::ToWireString(digest));
    return envelope.SerializeAsString();
}

inline EntryTicketVerifier Verifier() {
    return EntryTicketVerifier{{EntryTicketVerifier::Key{.id = kKeyId, .secret = Key()}}, kSkewMs};
}

}  // namespace sa::gateway::test
