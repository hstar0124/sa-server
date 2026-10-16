#pragma once

#include "sa/common/instance_id.hpp"
#include "sa/common/lease_keeper.hpp"

#include <asio/io_context.hpp>
#include <grpcpp/grpcpp.h>

#include <backend/world_directory.grpc.pb.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace sa::gateway {

// The gateway's view of WorldDirectory. Every call completes on the io_context thread.
class DirectoryClient {
public:
    using RedeemHandler = std::function<void(
        std::optional<backend::v1::RedeemEntryTicketResponse>)>;  // nullopt = RPC failed

    DirectoryClient(asio::io_context& io, std::shared_ptr<grpc::Channel> channel,
                    std::chrono::milliseconds rpc_timeout, common::InstanceId gateway_instance);

    void Redeem(backend::v1::RedeemEntryTicketRequest request, RedeemHandler on_done);
    // Fire and forget with logging: the Backend reaps sessions of dead gateways anyway.
    void EndSession(const common::InstanceId& session_id, std::string reason);
    // Adapter for the generic LeaseKeeper.
    void RenewGatewayLease(common::LeaseKeeper::Done done);

    [[nodiscard]] const common::InstanceId& gateway_instance() const noexcept {
        return gateway_instance_;
    }

private:
    asio::io_context& io_;
    std::unique_ptr<backend::v1::WorldDirectory::Stub> stub_;
    std::chrono::milliseconds rpc_timeout_;
    common::InstanceId gateway_instance_;
};

}  // namespace sa::gateway
