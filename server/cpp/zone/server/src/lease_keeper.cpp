#include "lease_keeper.hpp"

#include "sa/common/grpc_call.hpp"
#include "sa/common/wall_clock.hpp"

#include <spdlog/spdlog.h>

namespace sa::zone {

ZoneLeaseRenewer::ZoneLeaseRenewer(asio::io_context& io, std::shared_ptr<grpc::Channel> channel,
                                   std::chrono::milliseconds rpc_timeout, common::WorldId world,
                                   common::ZoneId zone, common::InstanceId instance)
    : io_(io),
      stub_(backend::v1::WorldDirectory::NewStub(std::move(channel))),
      rpc_timeout_(rpc_timeout),
      world_(world),
      zone_(zone),
      instance_(instance) {}

void ZoneLeaseRenewer::operator()(common::LeaseKeeper::Done done) {
    backend::v1::RenewZoneLeaseRequest request;
    request.mutable_zone()->set_world_id(world_.value());
    request.mutable_zone()->set_zone_id(zone_.value());
    request.mutable_zone()->set_zone_instance_id(instance_.ToWire());

    const common::WallTime sent_at = common::WallNow();
    common::CallUnary<backend::v1::WorldDirectory::Stub, backend::v1::RenewZoneLeaseRequest,
                      backend::v1::RenewZoneLeaseResponse>(
        io_.get_executor(), *stub_,
        &backend::v1::WorldDirectory::Stub::async_interface::RenewZoneLease, std::move(request),
        rpc_timeout_,
        [done = std::move(done), sent_at](const grpc::Status& status,
                                          const backend::v1::RenewZoneLeaseResponse& response) {
            if (!status.ok()) {
                spdlog::warn("RenewZoneLease: {} ({})", status.error_message(),
                             static_cast<int>(status.error_code()));
                done(std::nullopt);
                return;
            }
            if (!response.renewed()) {
                spdlog::error("RenewZoneLease refused: instance is bound to a different zone");
                done(std::nullopt);
                return;
            }
            done(common::LeaseKeeper::DurationFromServerExpiry(
                common::FromUnixMillis(response.lease_expires_at_ms()), sent_at));
        });
}

}  // namespace sa::zone
