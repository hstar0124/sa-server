#include "economy_client.hpp"

#include "sa/common/grpc_call.hpp"

#include <asio/steady_timer.hpp>
#include <spdlog/spdlog.h>

#include <memory>
#include <string_view>
#include <utility>

namespace sa::zone {
namespace {

// Failures that say nothing about the request itself; a repeat may well succeed.
bool IsTransient(const grpc::Status& status) {
    return status.error_code() == grpc::StatusCode::UNAVAILABLE ||
           status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED;
}

}  // namespace

EconomyClient::EconomyClient(asio::io_context& io, std::shared_ptr<grpc::Channel> channel,
                             EconomyClientConfig config)
    : io_(io), stub_(backend::v1::Economy::NewStub(std::move(channel))), config_(config) {}

void EconomyClient::RecordKill(backend::v1::RecordKillRequest request,
                               Reply<backend::v1::RecordKillResponse> reply) {
    Call<backend::v1::RecordKillRequest, backend::v1::RecordKillResponse>(
        std::make_shared<const backend::v1::RecordKillRequest>(std::move(request)),
        &backend::v1::Economy::Stub::async_interface::RecordKill, "RecordKill", 1,
        std::move(reply));
}

void EconomyClient::PickupItem(backend::v1::PickupItemRequest request,
                               Reply<backend::v1::PickupItemResponse> reply) {
    Call<backend::v1::PickupItemRequest, backend::v1::PickupItemResponse>(
        std::make_shared<const backend::v1::PickupItemRequest>(std::move(request)),
        &backend::v1::Economy::Stub::async_interface::PickupItem, "PickupItem", 1,
        std::move(reply));
}

void EconomyClient::GetCharacterAssets(backend::v1::GetCharacterAssetsRequest request,
                                       Reply<backend::v1::GetCharacterAssetsResponse> reply) {
    Call<backend::v1::GetCharacterAssetsRequest, backend::v1::GetCharacterAssetsResponse>(
        std::make_shared<const backend::v1::GetCharacterAssetsRequest>(std::move(request)),
        &backend::v1::Economy::Stub::async_interface::GetCharacterAssets, "GetCharacterAssets", 1,
        std::move(reply));
}

template <typename Request, typename Response>
void EconomyClient::Call(const std::shared_ptr<const Request>& request,
                         AsyncMethod<Request, Response> method, std::string_view name,
                         std::uint32_t attempt, Reply<Response> reply) {
    common::CallUnary<backend::v1::Economy::Stub, Request, Response>(
        io_.get_executor(), *stub_, method, *request, config_.rpc_timeout,
        [this, request, method, name, attempt, reply = std::move(reply)](
            const grpc::Status& status, Response response) mutable {
            if (status.ok()) {
                reply(std::move(response));
                return;
            }
            if (!IsTransient(status) || attempt >= config_.call_attempts) {
                spdlog::warn("{} failed after {} attempt(s): {} ({})", name, attempt,
                             status.error_message(), static_cast<int>(status.error_code()));
                reply(std::nullopt);
                return;
            }
            auto timer = std::make_shared<asio::steady_timer>(io_, config_.retry_delay);
            timer->async_wait([this, timer, request, method, name, attempt,
                               reply = std::move(reply)](const std::error_code& ec) mutable {
                if (ec) {
                    reply(std::nullopt);
                    return;
                }
                Call<Request, Response>(request, method, name, attempt + 1, std::move(reply));
            });
        });
}

}  // namespace sa::zone
