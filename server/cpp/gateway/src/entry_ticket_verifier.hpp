#pragma once

#include "sa/common/ids.hpp"
#include "sa/common/instance_id.hpp"
#include "sa/common/strong_id.hpp"
#include "sa/common/wall_clock.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <span>
#include <vector>

namespace sa::gateway {

enum class TicketError : std::uint8_t {
    kMalformed,
    kUnknownKey,
    kBadSignature,
    kExpired,
    kNotYetValid,
};

// Which signing key signed a ticket; keys rotate by id (ADR-0005).
using TicketKeyId = common::StrongId<struct TicketKeyIdTag, std::uint32_t>;

struct VerifiedTicket {
    common::InstanceId ticket_id;
    common::AccountId account;
    common::CharacterId character;
    common::WorldId world;
    common::WallTime expires_at;
};

// Local half of ADR-0005: signature and validity window. Single use and ownership are decided
// by the Backend. Pure: no I/O, time is an argument.
class EntryTicketVerifier {
public:
    struct Key {
        TicketKeyId id;
        std::vector<std::byte> secret;
    };

    // `max_clock_skew`: how far the issuing machine's clock may be from ours, either way.
    EntryTicketVerifier(std::vector<Key> keys, std::chrono::milliseconds max_clock_skew);

    [[nodiscard]] std::expected<VerifiedTicket, TicketError> Verify(
        std::span<const std::byte> signed_ticket, common::WallTime now) const;

private:
    std::map<TicketKeyId, std::vector<std::byte>> keys_;
    std::chrono::milliseconds max_clock_skew_;
};

}  // namespace sa::gateway
