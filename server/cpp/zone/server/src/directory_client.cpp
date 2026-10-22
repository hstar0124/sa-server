#include "directory_client.hpp"

#include "sa/common/grpc_call.hpp"

#include <memory>
#include <string_view>
#include <utility>

namespace sa::zone {

DirectoryClient::DirectoryClient(asio::io_context& io, std::shared_ptr<grpc::Channel> channel,
                                 BackendCallConfig config)
    : io_(io), stub_(backend::v1::WorldDirectory::NewStub(std::move(channel))), config_(config) {}

void DirectoryClient::SaveCheckpoint(backend::v1::SaveCheckpointRequest request,
                                     Reply<backend::v1::SaveCheckpointResponse> reply) {
    Call<backend::v1::SaveCheckpointRequest, backend::v1::SaveCheckpointResponse>(
        std::move(request), &backend::v1::WorldDirectory::Stub::async_interface::SaveCheckpoint,
        "SaveCheckpoint", std::move(reply));
}

void DirectoryClient::TransferCharacter(backend::v1::TransferCharacterRequest request,
                                        Reply<backend::v1::TransferCharacterResponse> reply) {
    Call<backend::v1::TransferCharacterRequest, backend::v1::TransferCharacterResponse>(
        std::move(request), &backend::v1::WorldDirectory::Stub::async_interface::TransferCharacter,
        "TransferCharacter", std::move(reply));
}

template <typename Request, typename Response>
void DirectoryClient::Call(Request request, AsyncMethod<Request, Response> method,
                           std::string_view name, Reply<Response> reply) {
    auto shared = std::make_shared<const Request>(std::move(request));
    RetryUntilAnswered<Response>(
        io_, config_, name,
        [this, shared, method](std::function<void(grpc::Status, Response)> done) {
            common::CallUnary<backend::v1::WorldDirectory::Stub, Request, Response>(
                io_.get_executor(), *stub_, method, *shared, config_.rpc_timeout, std::move(done));
        },
        std::move(reply));
}

}  // namespace sa::zone
