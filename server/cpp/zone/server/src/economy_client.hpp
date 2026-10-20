#pragma once

#include <asio/io_context.hpp>
#include <grpcpp/grpcpp.h>

#include <backend/economy.grpc.pb.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

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
    void Call(const std::shared_ptr<const Request>& request, AsyncMethod<Request, Response> method,
              std::string_view name, std::uint32_t attempt, Reply<Response> reply);

    asio::io_context& io_;
    std::unique_ptr<backend::v1::Economy::Stub> stub_;
    EconomyClientConfig config_;
};

}  // namespace sa::zone
