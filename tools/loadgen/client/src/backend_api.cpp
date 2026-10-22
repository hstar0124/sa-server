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

std::expected<common::CharacterId, ApiError> BackendApi::FindCharacter(
    const std::string& token, common::WorldId world, const std::string& name) const {
    const cpr::Response response =
        cpr::Get(cpr::Url{std::format("{}/api/v1/worlds/{}/characters", base_url_, world.value())},
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
            return common::CharacterId{character.value("characterId", std::uint64_t{0})};
        }
    }
    return std::unexpected(
        ApiError{std::format("character '{}' exists in the world but not on this account", name)});
}

std::expected<common::CharacterId, ApiError> BackendApi::EnsureCharacter(
    const std::string& token, common::WorldId world, const std::string& name,
    common::ClassId class_id) const {
    const json body{{"name", name}, {"classId", class_id.value()}};
    const cpr::Response response =
        cpr::Post(cpr::Url{std::format("{}/api/v1/worlds/{}/characters", base_url_, world.value())},
                  cpr::Bearer{token}, cpr::Header{{"content-type", "application/json"}},
                  cpr::Body{body.dump()});
    auto parsed = ParseBody(response, "create character");
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (response.status_code == kCreated) {
        return common::CharacterId{parsed->value("characterId", std::uint64_t{0})};
    }
    if (response.status_code == kConflict) {
        // Same seed, same name: a previous run created it. Reuse it if it is ours.
        return FindCharacter(token, world, name);
    }
    return std::unexpected(
        ApiError{std::format("create character: HTTP {} {}", response.status_code, response.text)});
}

std::expected<BotAccount, ApiError> BackendApi::Provision(const std::string& username,
                                                          const std::string& password,
                                                          common::WorldId world,
                                                          const std::string& character_name,
                                                          common::ClassId class_id) const {
    const json register_body{{"username", username}, {"password", password}};
    const cpr::Response registered = cpr::Post(cpr::Url{base_url_ + "/api/v1/auth/register"},
                                               cpr::Header{{"content-type", "application/json"}},
                                               cpr::Body{register_body.dump()});
    if (registered.error) {
        return std::unexpected(ApiError{"register: " + registered.error.message});
    }
    // A pinned identity seed reuses the accounts of an earlier run: 409 is expected then.
    if (registered.status_code != kCreated && registered.status_code != kConflict) {
        return std::unexpected(
            ApiError{std::format("register: HTTP {} {}", registered.status_code, registered.text)});
    }

    auto token = Login(username, password);
    if (!token) {
        return std::unexpected(token.error());
    }
    auto character = EnsureCharacter(*token, world, character_name, class_id);
    if (!character) {
        return std::unexpected(character.error());
    }
    return BotAccount{.token = std::move(*token), .character = *character};
}

std::expected<EntryGrant, ApiError> BackendApi::Enter(const BotAccount& account,
                                                      common::WorldId world) const {
    const cpr::Response entered =
        cpr::Post(cpr::Url{std::format("{}/api/v1/worlds/{}/characters/{}/enter", base_url_,
                                       world.value(), account.character.value())},
                  cpr::Bearer{account.token});
    auto parsed = ParseBody(entered, "enter");
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    if (entered.status_code != kOk) {
        return std::unexpected(
            ApiError{std::format("enter: HTTP {} {}", entered.status_code, entered.text)});
    }
    auto ticket = common::DecodeBase64(parsed->value("entryTicket", ""));
    const std::string host = (*parsed)["gateway"].value("host", "");
    const auto port = (*parsed)["gateway"].value("port", std::int64_t{0});
    if (!ticket || ticket->empty() || host.empty() || !std::in_range<std::uint16_t>(port) ||
        port == 0) {
        return std::unexpected(ApiError{"enter: response lacks a valid ticket or gateway address"});
    }
    EntryGrant grant;
    grant.signed_ticket = std::move(*ticket);
    grant.gateway.host = host;
    grant.gateway.port = static_cast<std::uint16_t>(port);
    return grant;
}

}  // namespace sa::loadgen
