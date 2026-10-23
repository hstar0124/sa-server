#include "trade_client.hpp"

#include "sa/common/grpc_call.hpp"

#include <memory>
#include <string_view>
#include <utility>

namespace sa::zone {

TradeClient::TradeClient(asio::io_context& io, std::shared_ptr<grpc::Channel> channel,
                         BackendCallConfig config)
    : io_(io), stub_(backend::v1::Trade::NewStub(std::move(channel))), config_(config) {}

void TradeClient::OpenTrade(backend::v1::OpenTradeRequest request,
                            Reply<backend::v1::TradeResponse> reply) {
    Call(std::move(request), &backend::v1::Trade::Stub::async_interface::OpenTrade, "OpenTrade",
         std::move(reply));
}

void TradeClient::OfferTrade(backend::v1::OfferTradeRequest request,
                             Reply<backend::v1::TradeResponse> reply) {
    Call(std::move(request), &backend::v1::Trade::Stub::async_interface::OfferTrade, "OfferTrade",
         std::move(reply));
}

void TradeClient::AcceptTrade(backend::v1::AcceptTradeRequest request,
                              Reply<backend::v1::TradeResponse> reply) {
    Call(std::move(request), &backend::v1::Trade::Stub::async_interface::AcceptTrade, "AcceptTrade",
         std::move(reply));
}

void TradeClient::CancelTrade(backend::v1::CancelTradeRequest request,
                              Reply<backend::v1::TradeResponse> reply) {
    Call(std::move(request), &backend::v1::Trade::Stub::async_interface::CancelTrade, "CancelTrade",
         std::move(reply));
}

template <typename Request>
void TradeClient::Call(Request request, AsyncMethod<Request> method, std::string_view name,
                       Reply<backend::v1::TradeResponse> reply) {
    auto shared = std::make_shared<const Request>(std::move(request));
    RetryUntilAnswered<backend::v1::TradeResponse>(
        io_, config_, name,
        [this, shared, method](std::function<void(grpc::Status, backend::v1::TradeResponse)> done) {
            common::CallUnary<backend::v1::Trade::Stub, Request, backend::v1::TradeResponse>(
                io_.get_executor(), *stub_, method, *shared, config_.rpc_timeout, std::move(done));
        },
        std::move(reply));
}

}  // namespace sa::zone
