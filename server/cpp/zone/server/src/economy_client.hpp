#pragma once

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include <backend/economy.grpc.pb.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace sa::zone {

// Zone-side view of the Economy service (contracts/backend/economy.proto). A reply of nullopt
// means no answer arrived (transport failure or deadline): the caller treats the outcome as
// unknown. An interface so ZoneHost can be tested against a scripted Backend.
class EconomyGateway {
public:
    template <typename Response>
    using Reply = std::function<void(std::optional<Response>)>;

    virtual ~EconomyGateway() = default;
    virtual void RecordKill(backend::v1::RecordKillRequest request,
                            Reply<backend::v1::RecordKillResponse> reply) = 0;
    virtual void PickupItem(backend::v1::PickupItemRequest request,
                            Reply<backend::v1::PickupItemResponse> reply) = 0;
    virtual void GetCharacterAssets(backend::v1::GetCharacterAssetsRequest request,
                                    Reply<backend::v1::GetCharacterAssetsResponse> reply) = 0;
};

struct EconomyClientConfig {
    std::chrono::milliseconds rpc_timeout{};
    // Every call is tried this many times in total on transport failures. All three are safe to
    // repeat: a kill carries its kill id, a pickup its request key, and an asset read changes
    // nothing. Only after the last attempt does the caller see "no answer".
    std::uint32_t call_attempts = 1;
    std::chrono::milliseconds retry_delay{};
};

// One try of a call: invokes `done` with the transport status and the response.
template <typename Response>
using Attempt = std::function<void(std::function<void(grpc::Status, Response)> done)>;

// Failures that say nothing about the request itself; a repeat may well succeed.
[[nodiscard]] bool IsTransient(const grpc::Status& status);

// The retry policy, apart from gRPC so it can be tested: runs `attempt` until it succeeds, fails
// for a reason a repeat will not fix, or has been tried `config.call_attempts` times, waiting
// `config.retry_delay` between tries. `reply` gets the response, or nullopt for "no answer".
template <typename Response>
void RetryUntilAnswered(asio::io_context& io, const EconomyClientConfig& config,
                        std::string_view name, const Attempt<Response>& attempt,
                        EconomyGateway::Reply<Response> reply, std::uint32_t number = 1) {
    auto again = attempt;  // the try below consumes nothing; a retry reuses the same request
    attempt([&io, &config, name, number, again = std::move(again), reply = std::move(reply)](
                const grpc::Status& status, Response response) mutable {
        if (status.ok()) {
            reply(std::move(response));
            return;
        }
        if (!IsTransient(status) || number >= config.call_attempts) {
            spdlog::warn("{} failed after {} attempt(s): {} ({})", name, number,
                         status.error_message(), static_cast<int>(status.error_code()));
            reply(std::nullopt);
            return;
        }
        auto timer = std::make_shared<asio::steady_timer>(io, config.retry_delay);
        timer->async_wait([&io, &config, name, number, timer, again = std::move(again),
                           reply = std::move(reply)](const std::error_code& ec) mutable {
            if (ec) {
                reply(std::nullopt);
                return;
            }
            RetryUntilAnswered<Response>(io, config, name, again, std::move(reply), number + 1);
        });
    });
}

// gRPC implementation; completions are delivered on the io_context thread (see CallUnary).
class EconomyClient final : public EconomyGateway {
public:
    EconomyClient(asio::io_context& io, std::shared_ptr<grpc::Channel> channel,
                  EconomyClientConfig config);

    void RecordKill(backend::v1::RecordKillRequest request,
                    Reply<backend::v1::RecordKillResponse> reply) override;
    void PickupItem(backend::v1::PickupItemRequest request,
                    Reply<backend::v1::PickupItemResponse> reply) override;
    void GetCharacterAssets(backend::v1::GetCharacterAssetsRequest request,
                            Reply<backend::v1::GetCharacterAssetsResponse> reply) override;

private:
    template <typename Request, typename Response>
    using AsyncMethod = void (backend::v1::Economy::Stub::async_interface::*)(
        grpc::ClientContext*, const Request*, Response*, std::function<void(grpc::Status)>);

    template <typename Request, typename Response>
    void Call(Request request, AsyncMethod<Request, Response> method, std::string_view name,
              Reply<Response> reply);

    asio::io_context& io_;
    std::unique_ptr<backend::v1::Economy::Stub> stub_;
    EconomyClientConfig config_;
};

}  // namespace sa::zone
