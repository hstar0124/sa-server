#pragma once

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

struct EntryGrant {
    std::vector<std::byte> signed_ticket;
    GatewayEndpoint gateway;
    std::uint64_t character_id = 0;
};

// Blocking REST client for the Backend's public API. Used only during provisioning, before the
// real-time part of the run begins. Provisioning is idempotent: re-running a seed reuses the
// accounts and characters it created before.
class BackendApi {
public:
    explicit BackendApi(std::string base_url);

    [[nodiscard]] std::expected<EntryGrant, ApiError> ProvisionAndEnter(
        const std::string& username, const std::string& password, std::uint64_t world_id,
        const std::string& character_name, std::uint32_t class_id) const;

private:
    [[nodiscard]] std::expected<std::string, ApiError> Login(const std::string& username,
                                                             const std::string& password) const;
    // Creates the character, or finds it when a previous run with the same seed already did.
    [[nodiscard]] std::expected<std::uint64_t, ApiError> EnsureCharacter(
        const std::string& token, std::uint64_t world_id, const std::string& name,
        std::uint32_t class_id) const;
    [[nodiscard]] std::expected<std::uint64_t, ApiError> FindCharacter(
        const std::string& token, std::uint64_t world_id, const std::string& name) const;

    std::string base_url_;
};

}  // namespace sa::loadgen
