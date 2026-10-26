#pragma once

#include "backend_call.hpp"

#include <asio/io_context.hpp>
#include <grpcpp/grpcpp.h>

#include <backend/trade.grpc.pb.h>

#include <memory>
#include <string_view>

namespace sa::zone {

// The zone's calls into the Backend's Trade service (ADR-0013). Every step is idempotent on its
// request key, so the shared retry policy applies. An interface so ZoneHost can be tested against
// a scripted Backend.
class TradeGateway {
public:
    template <typename Response>
    using Reply = BackendReply<Response>;

    virtual ~TradeGateway() = default;
    virtual void OpenTrade(backend::v1::OpenTradeRequest request,
                           Reply<backend::v1::TradeResponse> reply) = 0;
    virtual void OfferTrade(backend::v1::OfferTradeRequest request,
                            Reply<backend::v1::TradeResponse> reply) = 0;
    virtual void AcceptTrade(backend::v1::AcceptTradeRequest request,
                             Reply<backend::v1::TradeResponse> reply) = 0;
    virtual void CancelTrade(backend::v1::CancelTradeRequest request,
                             Reply<backend::v1::TradeResponse> reply) = 0;
};

// gRPC implementation; completions are delivered on the io_context thread (see CallUnary).
class TradeClient final : public TradeGateway {
public:
    TradeClient(asio::io_context& io, std::shared_ptr<grpc::Channel> channel,
                BackendCallConfig config);

    void OpenTrade(backend::v1::OpenTradeRequest request,
                   Reply<backend::v1::TradeResponse> reply) override;
    void OfferTrade(backend::v1::OfferTradeRequest request,
                    Reply<backend::v1::TradeResponse> reply) override;
    void AcceptTrade(backend::v1::AcceptTradeRequest request,
                     Reply<backend::v1::TradeResponse> reply) override;
    void CancelTrade(backend::v1::CancelTradeRequest request,
                     Reply<backend::v1::TradeResponse> reply) override;

private:
    template <typename Request>
    using AsyncMethod = void (backend::v1::Trade::Stub::async_interface::*)(
        grpc::ClientContext*, const Request*, backend::v1::TradeResponse*,
        std::function<void(grpc::Status)>);

    template <typename Request>
    void Call(Request request, AsyncMethod<Request> method, std::string_view name,
              Reply<backend::v1::TradeResponse> reply);

    asio::io_context& io_;
    std::unique_ptr<backend::v1::Trade::Stub> stub_;
    BackendCallConfig config_;
};

}  // namespace sa::zone
