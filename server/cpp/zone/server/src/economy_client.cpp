#include "economy_client.hpp"

#include "sa/common/grpc_call.hpp"

#include <asio/steady_timer.hpp>
#include <spdlog/spdlog.h>

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
    RecordKillAttempt(std::make_shared<backend::v1::RecordKillRequest>(std::move(request)), 1,
                      std::move(reply));
}

void EconomyClient::RecordKillAttempt(
    const std::shared_ptr<backend::v1::RecordKillRequest>& request, std::uint32_t attempt,
    Reply<backend::v1::RecordKillResponse> reply) {
    common::CallUnary<backend::v1::Economy::Stub, backend::v1::RecordKillRequest,
                      backend::v1::RecordKillResponse>(
        io_.get_executor(), *stub_, &backend::v1::Economy::Stub::async_interface::RecordKill,
        *request, config_.rpc_timeout,
        [this, request, attempt, reply = std::move(reply)](
            const grpc::Status& status, backend::v1::RecordKillResponse response) mutable {
            if (status.ok()) {
                reply(std::move(response));
                return;
            }
            if (!IsTransient(status) || attempt >= config_.kill_record_attempts) {
                spdlog::warn("RecordKill failed after {} attempt(s): {} ({})", attempt,
                             status.error_message(), static_cast<int>(status.error_code()));
                reply(std::nullopt);
                return;
            }
            auto timer = std::make_shared<asio::steady_timer>(io_, config_.retry_delay);
            timer->async_wait([this, timer, request, attempt,
                               reply = std::move(reply)](const std::error_code& ec) mutable {
                if (ec) {
                    reply(std::nullopt);
                    return;
                }
                RecordKillAttempt(request, attempt + 1, std::move(reply));
            });
        });
}

void EconomyClient::PickupItem(backend::v1::PickupItemRequest request,
                               Reply<backend::v1::PickupItemResponse> reply) {
    common::CallUnary<backend::v1::Economy::Stub, backend::v1::PickupItemRequest,
                      backend::v1::PickupItemResponse>(
        io_.get_executor(), *stub_, &backend::v1::Economy::Stub::async_interface::PickupItem,
        std::move(request), config_.rpc_timeout,
        [reply = std::move(reply)](const grpc::Status& status,
                                   backend::v1::PickupItemResponse response) {
            if (!status.ok()) {
                spdlog::warn("PickupItem failed: {} ({})", status.error_message(),
                             static_cast<int>(status.error_code()));
                reply(std::nullopt);
                return;
            }
            reply(std::move(response));
        });
}

void EconomyClient::GetCharacterAssets(backend::v1::GetCharacterAssetsRequest request,
                                       Reply<backend::v1::GetCharacterAssetsResponse> reply) {
    common::CallUnary<backend::v1::Economy::Stub, backend::v1::GetCharacterAssetsRequest,
                      backend::v1::GetCharacterAssetsResponse>(
        io_.get_executor(), *stub_,
        &backend::v1::Economy::Stub::async_interface::GetCharacterAssets, std::move(request),
        config_.rpc_timeout,
        [reply = std::move(reply)](const grpc::Status& status,
                                   backend::v1::GetCharacterAssetsResponse response) {
            if (!status.ok()) {
                spdlog::warn("GetCharacterAssets failed: {} ({})", status.error_message(),
                             static_cast<int>(status.error_code()));
                reply(std::nullopt);
                return;
            }
            reply(std::move(response));
        });
}

}  // namespace sa::zone
