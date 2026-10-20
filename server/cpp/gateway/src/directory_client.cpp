#include "directory_client.hpp"

#include "sa/common/grpc_call.hpp"
#include "sa/common/wall_clock.hpp"

#include <spdlog/spdlog.h>

namespace sa::gateway {

using backend::v1::WorldDirectory;

DirectoryClient::DirectoryClient(asio::io_context& io, std::shared_ptr<grpc::Channel> channel,
                                 std::chrono::milliseconds rpc_timeout,
                                 common::InstanceId gateway_instance)
    : io_(io),
      stub_(WorldDirectory::NewStub(std::move(channel))),
      rpc_timeout_(rpc_timeout),
      gateway_instance_(gateway_instance) {}

void DirectoryClient::Redeem(backend::v1::RedeemEntryTicketRequest request, RedeemHandler on_done) {
    request.set_gateway_instance_id(gateway_instance_.ToWire());
    common::CallUnary<WorldDirectory::Stub, backend::v1::RedeemEntryTicketRequest,
                      backend::v1::RedeemEntryTicketResponse>(
        io_.get_executor(), *stub_, &WorldDirectory::Stub::async_interface::RedeemEntryTicket,
        std::move(request), rpc_timeout_,
        [on_done = std::move(on_done)](const grpc::Status& status,
                                       const backend::v1::RedeemEntryTicketResponse& response) {
            if (!status.ok()) {
                spdlog::warn("RedeemEntryTicket: {} ({})", status.error_message(),
                             static_cast<int>(status.error_code()));
                on_done(std::nullopt);
                return;
            }
            on_done(response);
        });
}

void DirectoryClient::EndSession(const common::InstanceId& session_id, std::string reason) {
    backend::v1::EndSessionRequest request;
    request.set_gateway_session_id(session_id.ToWire());
    request.set_reason(std::move(reason));
    common::CallUnary<WorldDirectory::Stub, backend::v1::EndSessionRequest,
                      backend::v1::EndSessionResponse>(
        io_.get_executor(), *stub_, &WorldDirectory::Stub::async_interface::EndSession,
        std::move(request), rpc_timeout_,
        [id = session_id.ToHex()](const grpc::Status& status,
                                  const backend::v1::EndSessionResponse& response) {
            if (!status.ok()) {
                spdlog::warn("EndSession {}: {} ({})", id, status.error_message(),
                             static_cast<int>(status.error_code()));
            } else if (!response.ended()) {
                spdlog::debug("EndSession {}: already ended", id);
            }
        });
}

void DirectoryClient::RenewGatewayLease(common::LeaseKeeper::Done done) {
    backend::v1::RenewGatewayLeaseRequest request;
    request.set_gateway_instance_id(gateway_instance_.ToWire());
    const common::WallTime sent_at = common::WallNow();
    common::CallUnary<WorldDirectory::Stub, backend::v1::RenewGatewayLeaseRequest,
                      backend::v1::RenewGatewayLeaseResponse>(
        io_.get_executor(), *stub_, &WorldDirectory::Stub::async_interface::RenewGatewayLease,
        std::move(request), rpc_timeout_,
        [done = std::move(done), sent_at](const grpc::Status& status,
                                          const backend::v1::RenewGatewayLeaseResponse& response) {
            if (!status.ok() || !response.renewed()) {
                spdlog::warn("RenewGatewayLease: {} ({})", status.error_message(),
                             static_cast<int>(status.error_code()));
                done(std::nullopt);
                return;
            }
            done(common::LeaseKeeper::DurationFromServerExpiry(
                common::FromUnixMillis(response.lease_expires_at_ms()), sent_at));
        });
}

}  // namespace sa::gateway
