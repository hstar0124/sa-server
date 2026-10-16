#include "backend_api.hpp"

#include "sa/common/bytes.hpp"

#include <cpr/cpr.h>

#include <format>
#include <nlohmann/json.hpp>

namespace sa::loadgen {
namespace {

using json = nlohmann::json;

constexpr long kCreated = 201;
constexpr long kOk = 200;
constexpr long kConflict = 409;

std::expected<json, ApiError> ParseBody(const cpr::Response& response, const char* what) {
    if (response.error) {
        return std::unexpected(ApiError{std::format("{}: {}", what, response.error.message)});
    }
    try {
        return json::parse(response.text);
    } catch (const json::exception& e) {
        return std::unexpected(ApiError{std::format("{}: invalid JSON ({})", what, e.what())});
    }
}

}  // namespace

BackendApi::BackendApi(std::string base_url) : base_url_(std::move(base_url)) {}

std::expected<std::string, ApiError> BackendApi::Login(const std::string& username,
                                                       const std::string& password) const {
    const json body{{"username", username}, {"password", password}};
    const cpr::Response response =
        cpr::Post(cpr::Url{base_url_ + "/api/v1/auth/login"},
                  cpr::Header{{"content-type", "application/json"}}, cpr::Body{body.dump()});
    auto parsed = ParseBody(response, "login");
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (response.status_code != kOk) {
        return std::unexpected(
            ApiError{std::format("login: HTTP {} {}", response.status_code, response.text)});
    }
    return parsed->value("accessToken", "");
}

std::expected<std::uint64_t, ApiError> BackendApi::FindCharacter(const std::string& token,
                                                                 std::uint64_t world_id,
                                                                 const std::string& name) const {
    const cpr::Response response =
        cpr::Get(cpr::Url{std::format("{}/api/v1/worlds/{}/characters", base_url_, world_id)},
                 cpr::Bearer{token});
    auto parsed = ParseBody(response, "list characters");
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (response.status_code != kOk || !parsed->is_array()) {
        return std::unexpected(ApiError{
            std::format("list characters: HTTP {} {}", response.status_code, response.text)});
    }
    for (const json& character : *parsed) {
        if (character.value("name", "") == name) {
            return character.value("characterId", std::uint64_t{0});
        }
    }
    return std::unexpected(
        ApiError{std::format("character '{}' exists in the world but not on this account", name)});
}

std::expected<std::uint64_t, ApiError> BackendApi::EnsureCharacter(const std::string& token,
                                                                   std::uint64_t world_id,
                                                                   const std::string& name,
                                                                   std::uint32_t class_id) const {
    const json body{{"name", name}, {"classId", class_id}};
    const cpr::Response response =
        cpr::Post(cpr::Url{std::format("{}/api/v1/worlds/{}/characters", base_url_, world_id)},
                  cpr::Bearer{token}, cpr::Header{{"content-type", "application/json"}},
                  cpr::Body{body.dump()});
    auto parsed = ParseBody(response, "create character");
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (response.status_code == kCreated) {
        return parsed->value("characterId", std::uint64_t{0});
    }
    if (response.status_code == kConflict) {
        // Same seed, same name: a previous run created it. Reuse it if it is ours.
        return FindCharacter(token, world_id, name);
    }
    return std::unexpected(
        ApiError{std::format("create character: HTTP {} {}", response.status_code, response.text)});
}

std::expected<EntryGrant, ApiError> BackendApi::ProvisionAndEnter(const std::string& username,
                                                                  const std::string& password,
                                                                  std::uint64_t world_id,
                                                                  const std::string& character_name,
                                                                  std::uint32_t class_id) const {
    const json register_body{{"username", username}, {"password", password}};
    const cpr::Response registered = cpr::Post(cpr::Url{base_url_ + "/api/v1/auth/register"},
                                               cpr::Header{{"content-type", "application/json"}},
                                               cpr::Body{register_body.dump()});
    if (registered.error) {
        return std::unexpected(ApiError{"register: " + registered.error.message});
    }
    // Re-running the same seed reuses accounts: 409 is expected then.
    if (registered.status_code != kCreated && registered.status_code != kConflict) {
        return std::unexpected(
            ApiError{std::format("register: HTTP {} {}", registered.status_code, registered.text)});
    }

    auto token = Login(username, password);
    if (!token) {
        return std::unexpected(token.error());
    }
    auto character = EnsureCharacter(*token, world_id, character_name, class_id);
    if (!character) {
        return std::unexpected(character.error());
    }

    const cpr::Response entered =
        cpr::Post(cpr::Url{std::format("{}/api/v1/worlds/{}/characters/{}/enter", base_url_,
                                       world_id, *character)},
                  cpr::Bearer{*token});
    auto parsed = ParseBody(entered, "enter");
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (entered.status_code != kOk) {
        return std::unexpected(
            ApiError{std::format("enter: HTTP {} {}", entered.status_code, entered.text)});
    }
    EntryGrant grant;
    grant.signed_ticket =
        common::DecodeBase64(parsed->value("entryTicket", "")).value_or(std::vector<std::byte>{});
    grant.gateway.host = (*parsed)["gateway"].value("host", "");
    grant.gateway.port = static_cast<std::uint16_t>((*parsed)["gateway"].value("port", 0));
    grant.character_id = *character;
    if (grant.signed_ticket.empty() || grant.gateway.host.empty()) {
        return std::unexpected(ApiError{"enter: response lacks ticket or gateway"});
    }
    return grant;
}

}  // namespace sa::loadgen
