#pragma once

#include "sa/common/ids.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace sa::loadgen {

struct ApiError {
    std::string message;
};

struct GatewayEndpoint {
    std::string host;
    std::uint16_t port = 0;
};

// A bot's account session and character, ready to ask for an entry ticket.
struct BotAccount {
    std::string token;
    common::CharacterId character;
};

struct EntryGrant {
    std::vector<std::byte> signed_ticket;
    GatewayEndpoint gateway;
};

// Blocking REST client for the Backend's public API. Used only during provisioning, before the
// real-time part of the run begins. Provisioning is idempotent: re-running a seed reuses the
// accounts and characters it created before.
class BackendApi {
public:
    explicit BackendApi(std::string base_url);

    // Account and character (both reused when a previous run with the same seed made them). The
    // slow part (password hashing on register and login) happens here, before any ticket exists.
    [[nodiscard]] std::expected<BotAccount, ApiError> Provision(const std::string& username,
                                                                const std::string& password,
                                                                common::WorldId world,
                                                                const std::string& character_name,
                                                                common::ClassId class_id) const;
    // A fresh entry ticket. Tickets are short-lived, so ask right before connecting.
    [[nodiscard]] std::expected<EntryGrant, ApiError> Enter(const BotAccount& account,
                                                            common::WorldId world) const;

private:
    [[nodiscard]] std::expected<std::string, ApiError> Login(const std::string& username,
                                                             const std::string& password) const;
    // Creates the character, or finds it when a previous run with the same seed already did.
    [[nodiscard]] std::expected<common::CharacterId, ApiError> EnsureCharacter(
        const std::string& token, common::WorldId world, const std::string& name,
        common::ClassId class_id) const;
    [[nodiscard]] std::expected<common::CharacterId, ApiError> FindCharacter(
        const std::string& token, common::WorldId world, const std::string& name) const;

    std::string base_url_;
};

}  // namespace sa::loadgen
