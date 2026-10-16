#include "sa/common/config_source.hpp"
#include "sa/common/instance_id.hpp"
#include "sa/common/lease_keeper.hpp"
#include "sa/common/logging.hpp"

#include "client_session.hpp"
#include "directory_client.hpp"
#include "entry_ticket_verifier.hpp"
#include "gateway_config.hpp"
#include "listener.hpp"
#include "session_registry.hpp"
#include "zone_link.hpp"

#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <grpcpp/create_channel.h>
#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>

namespace {

constexpr std::string_view kUsage = "usage: gateway --config <path/to/gateway.toml>";

int Run(int argc, char** argv);

}  // namespace

// Asio and gRPC report unrecoverable start-up failures (port in use, bad address) as exceptions;
// they end the process with a logged message instead of std::terminate.
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
    if (args.size() != 3 || std::string_view{args[1]} != "--config") {
        std::cerr << kUsage << '\n';
        return EXIT_FAILURE;
    }

    auto source = sa::common::ConfigSource::Load(args[2], sa::gateway::GatewayConfig::kEnvPrefix);
    if (!source) {
        std::cerr << "config error: " << source.error().message << '\n';
        return EXIT_FAILURE;
    }
    auto config = sa::gateway::GatewayConfig::FromConfig(*source);
    if (!config) {
        std::cerr << "config error: " << config.error().message << '\n';
        return EXIT_FAILURE;
    }

    sa::common::InitLogging("gateway", config->logging);
    const sa::common::InstanceId gateway_instance = sa::common::InstanceId::Generate();
    spdlog::info("gateway_instance_id={} zones={}", gateway_instance.ToHex(), config->zones.size());

    asio::io_context io;

    // ---- Backend: directory RPCs and our own lease ----
    sa::gateway::DirectoryClient directory{
        io, grpc::CreateChannel(config->backend_address, grpc::InsecureChannelCredentials()),
        config->backend_rpc_timeout, gateway_instance};
    sa::common::LeaseKeeper lease{
        io, "gateway",
        sa::common::LeaseKeeperConfig{.renew_interval = config->lease_renew_interval,
                                      .safety_margin = config->lease_safety_margin},
        [&directory](sa::common::LeaseKeeper::Done done) {
            directory.RenewGatewayLease(std::move(done));
        }};

    // ---- zones ----
    sa::gateway::SessionRegistry registry;
    sa::gateway::ZoneRouter zones;
    for (const sa::gateway::ZoneEndpoint& endpoint : config->zones) {
        zones.Add(std::make_shared<sa::gateway::ZoneLink>(
            io,
            sa::gateway::ZoneLinkConfig{.zone = endpoint.zone,
                                        .address = endpoint.address,
                                        .port = endpoint.port,
                                        .frame_limits = config->zone_link_frame_limits,
                                        .reconnect_backoff = config->zone_link_reconnect_backoff},
            gateway_instance, registry));
    }

    // ---- clients ----
    const sa::gateway::EntryTicketVerifier verifier{
        {sa::gateway::EntryTicketVerifier::Key{.id = config->ticket_key_id,
                                               .secret = config->ticket_key}},
        config->ticket_max_clock_skew_ms};
    const sa::gateway::SessionProtocolConfig protocol{.max_messages_per_second =
                                                          config->max_messages_per_second};

    sa::gateway::Listener listener{
        io,
        sa::gateway::ListenerConfig{.address = config->listen_address,
                                    .port = config->listen_port,
                                    .max_connections = config->max_connections},
        [&, limits = config->frame_limits](asio::ip::tcp::socket socket) {
            return std::make_shared<sa::gateway::ClientSession>(
                std::move(socket), limits,
                sa::gateway::ClientSession::Dependencies{.verifier = verifier,
                                                         .protocol = protocol,
                                                         .directory = directory,
                                                         .zones = zones,
                                                         .registry = registry});
        }};

    // Shutdown: stop accepting, kick every session, then let pending writes and EndSession RPCs
    // drain for the grace period before the io_context is stopped .
    asio::steady_timer drain_deadline{io};
    asio::signal_set signals{io, SIGINT, SIGTERM};
    signals.async_wait([&](const std::error_code&, int signal) {
        spdlog::info("signal {} received, draining for {} ms", signal,
                     config->shutdown_grace.count());
        listener.Stop();
        lease.Stop();
        drain_deadline.expires_after(config->shutdown_grace);
        drain_deadline.async_wait([&](const std::error_code&) {
            zones.StopAll();
            io.stop();
        });
    });

    lease.Start();
    zones.StartAll();
    listener.Start();
    io.run();
    spdlog::info("stopped (sessions open at exit: {})", listener.open_sessions());
    return EXIT_SUCCESS;
}

}  // namespace
