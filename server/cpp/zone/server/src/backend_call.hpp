#pragma once

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

// The zone's retry policy for Backend calls, shared by the Economy and WorldDirectory clients.
// Every call the zone makes is safe to repeat: a kill carries its kill id, a pickup its request
// key, a transfer its transfer id, a checkpoint its version, and a read changes nothing.
namespace sa::zone {

// nullopt means no answer arrived (transport failure or deadline, after every attempt): the
// caller treats the outcome as unknown.
template <typename Response>
using BackendReply = std::function<void(std::optional<Response>)>;

struct BackendCallConfig {
    std::chrono::milliseconds rpc_timeout{};
    // Every call is tried this many times in total on transport failures. Only after the last
    // attempt does the caller see "no answer".
    std::uint32_t call_attempts = 1;
    std::chrono::milliseconds retry_delay{};
};

// One try of a call: invokes `done` with the transport status and the response.
template <typename Response>
using Attempt = std::function<void(std::function<void(grpc::Status, Response)> done)>;

// Failures that say nothing about the request itself; a repeat may well succeed.
[[nodiscard]] inline bool IsTransient(const grpc::Status& status) {
    return status.error_code() == grpc::StatusCode::UNAVAILABLE ||
           status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED;
}

// Apart from gRPC so it can be tested: runs `attempt` until it succeeds, fails for a reason a
// repeat will not fix, or has been tried `config.call_attempts` times, waiting
// `config.retry_delay` between tries. `reply` gets the response, or nullopt for "no answer".
template <typename Response>
void RetryUntilAnswered(asio::io_context& io, const BackendCallConfig& config,
                        std::string_view name, const Attempt<Response>& attempt,
                        BackendReply<Response> reply, std::uint32_t number = 1) {
    auto again = attempt;  // the try below consumes nothing; a retry reuses the same request
    attempt([&io, &config, name, number, again = std::move(again), reply = std::move(reply)](
                const grpc::Status& status, Response response) mutable {
        if (status.ok()) {
            reply(std::move(response));
            return;
        }
        if (!IsTransient(status) || number >= config.call_attempts) {
            spdlog::warn("{} failed after {} attempt(s): {} ({})", name, number,
                         status.error_message(), static_cast<int>(status.error_code()));
            reply(std::nullopt);
            return;
        }
        auto timer = std::make_shared<asio::steady_timer>(io, config.retry_delay);
        timer->async_wait([&io, &config, name, number, timer, again = std::move(again),
                           reply = std::move(reply)](const std::error_code& ec) mutable {
            if (ec) {
                reply(std::nullopt);
                return;
            }
            RetryUntilAnswered<Response>(io, config, name, again, std::move(reply), number + 1);
        });
    });
}

}  // namespace sa::zone
