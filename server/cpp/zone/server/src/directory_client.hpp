#pragma once

#include "backend_call.hpp"

#include <asio/io_context.hpp>
#include <grpcpp/grpcpp.h>

#include <backend/world_directory.grpc.pb.h>

#include <memory>
#include <string_view>

namespace sa::zone {

// The zone's placement calls into WorldDirectory (ADR-0010): where the character stands, and
// handing it to another zone. The lease has its own keeper. An interface so ZoneHost can be
// tested against a scripted Backend.
class DirectoryGateway {
public:
    template <typename Response>
    using Reply = BackendReply<Response>;

    virtual ~DirectoryGateway() = default;
    virtual void SaveCheckpoint(backend::v1::SaveCheckpointRequest request,
                                Reply<backend::v1::SaveCheckpointResponse> reply) = 0;
    virtual void TransferCharacter(backend::v1::TransferCharacterRequest request,
                                   Reply<backend::v1::TransferCharacterResponse> reply) = 0;
};

// gRPC implementation; completions are delivered on the io_context thread (see CallUnary).
class DirectoryClient final : public DirectoryGateway {
public:
    DirectoryClient(asio::io_context& io, std::shared_ptr<grpc::Channel> channel,
                    BackendCallConfig config);

    void SaveCheckpoint(backend::v1::SaveCheckpointRequest request,
                        Reply<backend::v1::SaveCheckpointResponse> reply) override;
    void TransferCharacter(backend::v1::TransferCharacterRequest request,
                           Reply<backend::v1::TransferCharacterResponse> reply) override;

private:
    template <typename Request, typename Response>
    using AsyncMethod = void (backend::v1::WorldDirectory::Stub::async_interface::*)(
        grpc::ClientContext*, const Request*, Response*, std::function<void(grpc::Status)>);

    template <typename Request, typename Response>
    void Call(Request request, AsyncMethod<Request, Response> method, std::string_view name,
              Reply<Response> reply);

    asio::io_context& io_;
    std::unique_ptr<backend::v1::WorldDirectory::Stub> stub_;
    BackendCallConfig config_;
};

}  // namespace sa::zone
