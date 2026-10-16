#pragma once

#include "sa/common/ids.hpp"
#include "sa/common/instance_id.hpp"

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

struct VerifiedTicket {
    common::InstanceId ticket_id;
    common::AccountId account;
    common::CharacterId character;
    common::WorldId world;
    std::uint64_t expires_at_ms = 0;
};

// Local half of ADR-0005: signature and validity window. Single use and ownership are decided
// by the Backend. Pure: no I/O, time is an argument.
class EntryTicketVerifier {
public:
    struct Key {
        std::uint32_t id;
        std::vector<std::byte> secret;
    };

    EntryTicketVerifier(std::vector<Key> keys, std::uint64_t max_clock_skew_ms);

    [[nodiscard]] std::expected<VerifiedTicket, TicketError> Verify(
        std::span<const std::byte> signed_ticket, std::uint64_t now_ms) const;

private:
    std::map<std::uint32_t, std::vector<std::byte>> keys_;
    std::uint64_t max_clock_skew_ms_;
};

}  // namespace sa::gateway
