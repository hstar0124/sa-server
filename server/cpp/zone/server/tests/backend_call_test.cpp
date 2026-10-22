#include "backend_call.hpp"

#include <asio/post.hpp>
#include <gtest/gtest.h>

#include <backend/economy.pb.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace sa::zone {
namespace {

using Response = backend::v1::RecordKillResponse;

constexpr std::chrono::milliseconds kTestRetryDelay{1};
constexpr std::uint32_t kTestAttempts = 3;

// Answers each try with the next scripted status, asynchronously like gRPC.
struct ScriptedBackend {
    asio::io_context& io;
    std::vector<grpc::Status> statuses;
    std::uint32_t tries = 0;

    Attempt<Response> AsAttempt() {
        return [this](std::function<void(grpc::Status, Response)> done) {
            const grpc::Status status = statuses.at(tries);
            ++tries;
            asio::post(io, [status, done = std::move(done)] { done(status, Response{}); });
        };
    }
};

const grpc::Status kUnavailable{grpc::StatusCode::UNAVAILABLE, "down"};
const grpc::Status kInvalid{grpc::StatusCode::INVALID_ARGUMENT, "bad request"};

struct Outcome {
    bool replied = false;
    std::optional<Response> response;
};

Outcome CallThrough(asio::io_context& io, ScriptedBackend& backend) {
    const BackendCallConfig config{.rpc_timeout = std::chrono::milliseconds{1},
                                   .call_attempts = kTestAttempts,
                                   .retry_delay = kTestRetryDelay};
    Outcome outcome;
    RetryUntilAnswered<Response>(io, config, "RecordKill", backend.AsAttempt(),
                                 [&outcome](std::optional<Response> response) {
                                     outcome.replied = true;
                                     outcome.response = std::move(response);
                                 });
    io.run();
    return outcome;
}

TEST(BackendRetry, TransientFailuresAreRetriedUntilAnAnswer) {
    asio::io_context io;
    ScriptedBackend backend{.io = io, .statuses = {kUnavailable, kUnavailable, grpc::Status::OK}};
    const Outcome outcome = CallThrough(io, backend);
    EXPECT_EQ(backend.tries, 3U);
    ASSERT_TRUE(outcome.replied);
    EXPECT_TRUE(outcome.response.has_value());
}

TEST(BackendRetry, AFailureARepeatCannotFixIsNotRetried) {
    asio::io_context io;
    ScriptedBackend backend{.io = io, .statuses = {kInvalid}};
    const Outcome outcome = CallThrough(io, backend);
    EXPECT_EQ(backend.tries, 1U);
    ASSERT_TRUE(outcome.replied);
    EXPECT_FALSE(outcome.response.has_value()) << "no answer: the caller treats it as unknown";
}

TEST(BackendRetry, NoAnswerAfterTheLastAttempt) {
    asio::io_context io;
    ScriptedBackend backend{.io = io, .statuses = {kUnavailable, kUnavailable, kUnavailable}};
    const Outcome outcome = CallThrough(io, backend);
    EXPECT_EQ(backend.tries, kTestAttempts);
    ASSERT_TRUE(outcome.replied);
    EXPECT_FALSE(outcome.response.has_value());
}

}  // namespace
}  // namespace sa::zone
