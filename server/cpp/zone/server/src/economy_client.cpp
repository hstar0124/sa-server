#include "economy_client.hpp"

#include "sa/common/grpc_call.hpp"

#include <asio/steady_timer.hpp>
#include <spdlog/spdlog.h>

#include <memory>
#include <string_view>
#include <utility>

namespace sa::zone {

bool IsTransient(const grpc::Status& status) {
    return status.error_code() == grpc::StatusCode::UNAVAILABLE ||
           status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED;
}

EconomyClient::EconomyClient(asio::io_context& io, std::shared_ptr<grpc::Channel> channel,
                             EconomyClientConfig config)
    : io_(io), stub_(backend::v1::Economy::NewStub(std::move(channel))), config_(config) {}

void EconomyClient::RecordKill(backend::v1::RecordKillRequest request,
                               Reply<backend::v1::RecordKillResponse> reply) {
    Call<backend::v1::RecordKillRequest, backend::v1::RecordKillResponse>(
        std::move(request), &backend::v1::Economy::Stub::async_interface::RecordKill, "RecordKill",
        std::move(reply));
}

void EconomyClient::PickupItem(backend::v1::PickupItemRequest request,
                               Reply<backend::v1::PickupItemResponse> reply) {
    Call<backend::v1::PickupItemRequest, backend::v1::PickupItemResponse>(
        std::move(request), &backend::v1::Economy::Stub::async_interface::PickupItem, "PickupItem",
        std::move(reply));
}

void EconomyClient::GetCharacterAssets(backend::v1::GetCharacterAssetsRequest request,
                                       Reply<backend::v1::GetCharacterAssetsResponse> reply) {
    Call<backend::v1::GetCharacterAssetsRequest, backend::v1::GetCharacterAssetsResponse>(
        std::move(request), &backend::v1::Economy::Stub::async_interface::GetCharacterAssets,
        "GetCharacterAssets", std::move(reply));
}

template <typename Request, typename Response>
void EconomyClient::Call(Request request, AsyncMethod<Request, Response> method,
                         std::string_view name, Reply<Response> reply) {
    auto shared = std::make_shared<const Request>(std::move(request));
    RetryUntilAnswered<Response>(
        io_, config_, name,
        [this, shared, method](std::function<void(grpc::Status, Response)> done) {
            common::CallUnary<backend::v1::Economy::Stub, Request, Response>(
                io_.get_executor(), *stub_, method, *shared, config_.rpc_timeout, std::move(done));
        },
        std::move(reply));
}

}  // namespace sa::zone
