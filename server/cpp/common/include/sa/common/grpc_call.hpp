#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/post.hpp>
#include <grpcpp/grpcpp.h>

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

namespace sa::common {

// Runs one unary RPC through gRPC's callback API and delivers the result on an Asio executor,
// so the zone/gateway thread never blocks on the network and never touches state from a gRPC
// thread. `method` is e.g. &WorldDirectory::StubInterface::async_interface::RedeemEntryTicket.
//
// The call counts as outstanding work on the executor until its result is posted, so an
// io_context left to run out of work does not return (and get destroyed) under a call that
// would post into it later.
template <typename Stub, typename Request, typename Response>
void CallUnary(const asio::any_io_executor& executor, Stub& stub,
               void (Stub::async_interface::*method)(grpc::ClientContext*, const Request*,
                                                     Response*, std::function<void(grpc::Status)>),
               Request request, std::chrono::milliseconds deadline,
               std::function<void(grpc::Status, Response)> on_done) {
    struct Call {
        grpc::ClientContext context;
        Request request;
        Response response;
    };
    auto call = std::make_shared<Call>();
    call->request = std::move(request);
    call->context.set_deadline(std::chrono::system_clock::now() + deadline);
    // The callback runs on a gRPC thread; hop back to the owner's executor before touching state.
    (stub.async()->*method)(
        &call->context, &call->request, &call->response,
        [executor, call, work = asio::make_work_guard(executor),
         on_done = std::move(on_done)](grpc::Status status) mutable {
            asio::post(executor,
                       [call, status = std::move(status), on_done = std::move(on_done)]() mutable {
                           on_done(std::move(status), std::move(call->response));
                       });
            work.reset();  // the posted result is the executor's work from here on
        });
}

}  // namespace sa::common
