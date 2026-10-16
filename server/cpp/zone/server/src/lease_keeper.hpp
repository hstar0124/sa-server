#pragma once

#include "sa/common/ids.hpp"
#include "sa/common/instance_id.hpp"
#include "sa/common/lease_keeper.hpp"

#include <asio/io_context.hpp>
#include <grpcpp/grpcpp.h>

#include <backend/world_directory.grpc.pb.h>

#include <chrono>
#include <memory>

namespace sa::zone {

// Supplies the RenewZoneLease RPC to the generic LeaseKeeper.
class ZoneLeaseRenewer {
public:
    ZoneLeaseRenewer(asio::io_context& io, std::shared_ptr<grpc::Channel> channel,
                     std::chrono::milliseconds rpc_timeout, common::WorldId world,
                     common::ZoneId zone, common::InstanceId instance);

    void operator()(common::LeaseKeeper::Done done);

private:
    asio::io_context& io_;
    std::unique_ptr<backend::v1::WorldDirectory::Stub> stub_;
    std::chrono::milliseconds rpc_timeout_;
    common::WorldId world_;
    common::ZoneId zone_;
    common::InstanceId instance_;
};

}  // namespace sa::zone
