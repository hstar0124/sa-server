#include "sa/bot/fighter.hpp"
#include "sa/bot/looter.hpp"
#include "sa/bot/wanderer.hpp"
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
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kUsage = "usage: loadgen --profile <path/to/profile.toml>";

std::uint64_t ResolveIdentitySeed(std::optional<std::uint64_t> configured) {
    if (configured) {
        return *configured;
    }
    std::random_device device;
    return (static_cast<std::uint64_t>(device()) << 32U) | device();
}

// Phase 1, blocking HTTP before the clock starts: accounts and characters. Register and login
// hash passwords, so this is the slow part; no ticket exists yet that could expire meanwhile.
std::vector<std::optional<sa::loadgen::BotAccount>> Provision(
    const sa::loadgen::LoadProfile& profile, std::uint64_t identity_seed,
    const sa::loadgen::BackendApi& api, std::vector<sa::loadgen::BotReport>& reports) {
    std::vector<std::optional<sa::loadgen::BotAccount>> accounts(profile.bot_count());
    for (std::uint32_t i = 0; i < profile.bot_count(); ++i) {
        const sa::loadgen::BotIdentity identity =
            sa::loadgen::LoadProfile::IdentityOf(profile.account_prefix, identity_seed, i);
        reports[i].index = i;
        reports[i].username = identity.username;
        auto account = api.Provision(identity.username, profile.password, profile.world,
                                     identity.character_name, profile.class_id);
        if (!account) {
            reports[i].failure = account.error().message;
            spdlog::warn("bot {} provisioning failed: {}", i, account.error().message);
            continue;
        }
        accounts[i] = std::move(*account);
    }
    return accounts;
}

// Phase 2, right before the run: one short-lived entry ticket per bot. Entering hashes nothing,
// so even a thousand bots get their tickets well within the ticket lifetime.
void Enter(const sa::loadgen::LoadProfile& profile, const sa::loadgen::BackendApi& api,
           const std::vector<std::optional<sa::loadgen::BotAccount>>& accounts,
           std::vector<sa::loadgen::BotReport>& reports,
           std::vector<sa::loadgen::BotSession::Target>& targets) {
    for (std::uint32_t i = 0; i < profile.bot_count(); ++i) {
        const std::optional<sa::loadgen::BotAccount>& account = accounts[i];
        if (!account) {
            continue;
        }
        auto grant = api.Enter(*account, profile.world);
        if (!grant) {
            reports[i].failure = grant.error().message;
            spdlog::warn("bot {} entry failed: {}", i, grant.error().message);
            continue;
        }
        reports[i].provisioned = true;
        targets[i] = sa::loadgen::BotSession::Target{
            .host = profile.gateway_host.value_or(grant->gateway.host),
            .port = profile.gateway_port.value_or(grant->gateway.port),
            .signed_ticket = std::move(grant->signed_ticket)};
    }
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
    const auto logging = sa::common::LoggingConfig::FromConfig(*source);
    if (!logging) {
        std::cerr << "profile error: " << logging.error().message << '\n';
        return EXIT_FAILURE;
    }
    sa::common::InitLogging("loadgen", *logging);
    const std::uint64_t identity_seed = ResolveIdentitySeed(profile->identity_seed);
    spdlog::info(
        "seed={} identity_seed={} wanderers={} fighters={} looters={} duration={}s "
        "backend={}",
        profile->seed, identity_seed, profile->wanderers, profile->fighters, profile->looters,
        profile->duration.count(), profile->backend_base_url);

    std::vector<sa::loadgen::BotReport> reports(profile->bot_count());
    std::vector<sa::loadgen::BotSession::Target> targets(
        profile->bot_count());  // valid where provisioned
    const sa::loadgen::BackendApi api{profile->backend_base_url};
    const auto accounts = Provision(*profile, identity_seed, api, reports);
    Enter(*profile, api, accounts, reports, targets);
    spdlog::info("provisioned {}/{} bots",
                 sa::loadgen::RunReport::Aggregate(reports, profile->criteria).provisioned,
                 profile->bot_count());

    // ---- the run ----
    asio::io_context io;
    std::vector<std::shared_ptr<sa::loadgen::BotSession>> sessions;
    for (std::uint32_t i = 0; i < profile->bot_count(); ++i) {
        if (!reports[i].provisioned) {
            continue;
        }
        std::unique_ptr<sa::bot::Brain> brain;
        switch (profile->RoleOf(i)) {
            case sa::loadgen::BotRole::kWanderer:
                brain = std::make_unique<sa::bot::Wanderer>(profile->seed + i, profile->wanderer);
                break;
            case sa::loadgen::BotRole::kFighter:
                brain = std::make_unique<sa::bot::Fighter>(profile->seed + i, profile->fighter);
                break;
            case sa::loadgen::BotRole::kLooter:
                brain = std::make_unique<sa::bot::Looter>(profile->seed + i, profile->looter);
                break;
        }
        sessions.push_back(std::make_shared<sa::loadgen::BotSession>(io, *profile, std::move(brain),
                                                                     targets[i], reports[i]));
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
        sa::loadgen::RunReport::Aggregate(reports, profile->criteria);
    std::cout << report.ToText();
    for (const sa::loadgen::BotReport& b : reports) {
        if (!b.failure.empty()) {
            std::cout << std::format("  bot {} ({}): {}\n", b.index, b.username, b.failure);
        }
    }
    return report.Succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace
