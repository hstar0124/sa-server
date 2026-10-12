#include "sa/common/config_source.hpp"
#include "sa/common/logging.hpp"

#include "backend_api.hpp"
#include "bot_session.hpp"
#include "profile.hpp"
#include "report.hpp"

#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <memory>
#include <random>
#include <span>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kUsage = "usage: loadgen --profile <path/to/profile.toml>";

// Deterministic per-bot identities: the same seed provisions the same accounts (ADR-0006).
std::string BotUsername(const sa::loadgen::LoadProfile& profile, std::uint32_t index) {
    std::mt19937_64 rng{profile.seed ^ (static_cast<std::uint64_t>(index) << 32U)};
    return std::format("{}_{:08x}", profile.account_prefix, static_cast<std::uint32_t>(rng()));
}

std::string BotCharacterName(std::uint64_t seed, std::uint32_t index) {
    std::mt19937_64 rng{seed + index};
    return std::format("Bot{:06x}", static_cast<std::uint32_t>(rng()) & 0xFFFFFFU);
}

int Run(int argc, char** argv);

}  // namespace

int main(int argc, char** argv) {
    try {
        return Run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

namespace {

int Run(int argc, char** argv) {
    const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
    if (args.size() != 3 || std::string_view{args[1]} != "--profile") {
        std::cerr << kUsage << '\n';
        return EXIT_FAILURE;
    }
    auto source = sa::common::ConfigSource::Load(args[2], sa::loadgen::LoadProfile::kEnvPrefix);
    if (!source) {
        std::cerr << "profile error: " << source.error().message << '\n';
        return EXIT_FAILURE;
    }
    auto profile = sa::loadgen::LoadProfile::FromConfig(*source);
    if (!profile) {
        std::cerr << "profile error: " << profile.error().message << '\n';
        return EXIT_FAILURE;
    }
    sa::common::InitLogging("loadgen", sa::common::LoggingConfig::FromConfig(*source).value_or(
                                           sa::common::LoggingConfig{}));
    spdlog::info("seed={} bots={} duration={}s backend={}", profile->seed, profile->bot_count,
                 profile->duration.count(), profile->backend_base_url);

    // ---- provisioning (blocking HTTP, before the clock starts) ----
    std::vector<sa::loadgen::BotReport> reports(profile->bot_count);
    std::vector<sa::loadgen::BotSession::Target> targets(
        profile->bot_count);  // valid where provisioned
    const sa::loadgen::BackendApi api{profile->backend_base_url};
    for (std::uint32_t i = 0; i < profile->bot_count; ++i) {
        reports[i].index = i;
        reports[i].username = BotUsername(*profile, i);
        auto grant =
            api.ProvisionAndEnter(reports[i].username, profile->password, profile->world_id,
                                  BotCharacterName(profile->seed, i), profile->class_id);
        if (!grant) {
            reports[i].failure = grant.error().message;
            spdlog::warn("bot {} provisioning failed: {}", i, grant.error().message);
            continue;
        }
        reports[i].provisioned = true;
        targets[i] = sa::loadgen::BotSession::Target{
            .host = profile->gateway_host.empty() ? grant->gateway.host : profile->gateway_host,
            .port = profile->gateway_port == 0 ? grant->gateway.port : profile->gateway_port,
            .signed_ticket = std::move(grant->signed_ticket)};
    }
    spdlog::info(
        "provisioned {}/{} bots",
        sa::loadgen::RunReport::Aggregate(reports, profile->min_bots_seeing_others).provisioned,
        profile->bot_count);

    // ---- the run ----
    asio::io_context io;
    std::vector<std::shared_ptr<sa::loadgen::BotSession>> sessions;
    for (std::uint32_t i = 0; i < profile->bot_count; ++i) {
        if (!reports[i].provisioned) {
            continue;
        }
        sessions.push_back(std::make_shared<sa::loadgen::BotSession>(
            io, *profile, profile->seed + i, targets[i], reports[i]));
    }
    for (const auto& session : sessions) {
        session->Start();
    }

    auto stop_all = [&] {
        for (const auto& session : sessions) {
            session->Stop();
        }
    };
    // Whichever fires first ends the run; the other wait is cancelled so io.run() can return
    // once the sockets have drained.
    asio::steady_timer deadline{io};
    asio::signal_set signals{io, SIGINT, SIGTERM};
    deadline.expires_after(profile->duration);
    deadline.async_wait([&](const std::error_code& ec) {
        if (!ec) {
            spdlog::info("duration elapsed, closing sessions");
            signals.cancel();
            stop_all();
        }
    });
    signals.async_wait([&](const std::error_code& ec, int) {
        if (!ec) {
            spdlog::info("interrupted, closing sessions");
            deadline.cancel();
            stop_all();
        }
    });
    io.run();

    const sa::loadgen::RunReport report =
        sa::loadgen::RunReport::Aggregate(reports, profile->min_bots_seeing_others);
    std::cout << report.ToText();
    for (const sa::loadgen::BotReport& b : reports) {
        if (!b.failure.empty()) {
            std::cout << std::format("  bot {} ({}): {}\n", b.index, b.username, b.failure);
        }
    }
    return report.Succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace
