#include "sa/common/config_source.hpp"
#include "sa/common/instance_id.hpp"
#include "sa/common/lease_keeper.hpp"
#include "sa/common/logging.hpp"
#include "sa/common/protocol_limits.hpp"
#include "sa/zone/data/game_data_loader.hpp"
#include "sa/zone/sim/fixed_step_scheduler.hpp"
#include "sa/zone/sim/rng.hpp"
#include "sa/zone/sim/zone.hpp"

#include "directory_client.hpp"
#include "economy_client.hpp"
#include "gateway_link_server.hpp"
#include "lease_keeper.hpp"
#include "local_bot_host.hpp"
#include "tick_runner.hpp"
#include "zone_host.hpp"
#include "zone_server_config.hpp"

#include <asio/io_context.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <grpcpp/create_channel.h>
#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <random>
#include <span>
#include <string_view>

namespace {

constexpr std::string_view kUsage = "usage: zone_server --config <path/to/zone.toml>";

std::uint64_t ResolveSeed(std::optional<std::uint64_t> configured) {
    if (configured) {
        return *configured;
    }
    std::random_device device;
    return (static_cast<std::uint64_t>(device()) << 32U) | device();
}

// Periodic interest-management totals for the dev bots, alongside the tick statistics.
class BotStatsLogger {
public:
    BotStatsLogger(asio::io_context& io, const sa::zone::LocalBotHost& bots,
                   std::chrono::seconds interval)
        : timer_(io), bots_(bots), interval_(interval) {}

    void Start() { Schedule(); }
    void Stop() { timer_.cancel(); }

private:
    void Schedule() {
        timer_.expires_after(interval_);
        timer_.async_wait([this](const std::error_code& ec) {
            if (ec) {
                return;
            }
            const auto& t = bots_.totals();
            const double per_view = t.views == 0 ? 0.0
                                                 : static_cast<double>(t.spawned + t.updated) /
                                                       static_cast<double>(t.views);
            spdlog::info("views={} spawned={} updated={} despawned={} deltas_per_view={:.2f}",
                         t.views, t.spawned, t.updated, t.despawned, per_view);
            Schedule();
        });
    }

    asio::steady_timer timer_;
    const sa::zone::LocalBotHost& bots_;
    std::chrono::seconds interval_;
};

// Loads everything the zone reads from game-data; any failure is fatal at start-up.
struct LoadedWorld {
    sa::zone::sim::MapData map;
    sa::zone::sim::ZoneRules rules;
    sa::zone::sim::ContentBundle content;
};

std::optional<LoadedWorld> LoadWorld(const sa::zone::ZoneServerConfig& config) {
    auto map = sa::zone::data::LoadMap(config.map_file);
    auto movement = sa::zone::data::LoadMovementRules(config.movement_rules_file);
    auto combat = sa::zone::data::LoadCombatRules(config.movement_rules_file);
    auto economy = sa::zone::data::LoadEconomyRules(config.economy_rules_file);
    auto content = sa::zone::data::LoadContent(config.content_dir);
    for (const sa::zone::data::LoadError* err :
         {map ? nullptr : &map.error(), movement ? nullptr : &movement.error(),
          combat ? nullptr : &combat.error(), economy ? nullptr : &economy.error(),
          content ? nullptr : &content.error()}) {
        if (err != nullptr) {
            spdlog::critical("game-data: {}", err->message);
            return std::nullopt;
        }
    }
    return LoadedWorld{.map = std::move(*map),
                       .rules = sa::zone::sim::ZoneRules{.movement = *movement,
                                                         .combat = *combat,
                                                         .economy = std::move(*economy)},
                       .content = std::move(*content)};
}

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

    auto source = sa::common::ConfigSource::Load(args[2], sa::zone::ZoneServerConfig::kEnvPrefix);
    if (!source) {
        std::cerr << "config error: " << source.error().message << '\n';
        return EXIT_FAILURE;
    }
    auto config = sa::zone::ZoneServerConfig::FromConfig(*source);
    if (!config) {
        std::cerr << "config error: " << config.error().message << '\n';
        return EXIT_FAILURE;
    }

    sa::common::InitLogging("zone", config->logging);

    auto world = LoadWorld(*config);
    if (!world) {
        return EXIT_FAILURE;
    }

    const std::uint64_t seed = ResolveSeed(config->rng_seed);
    spdlog::info(
        "starting world_id={} zone_id={} hz={} snapshot_hz={} map={}x{} obstacles={} "
        "monster_spawns={} "
        "content={} seed={}",
        config->world_id.value(), config->zone_id.value(), config->simulation_rate.hz,
        config->snapshot_rate.hz, world->map.size.x, world->map.size.y, world->map.obstacles.size(),
        world->map.monster_spawns.size(), world->content.version, seed);

    auto created = sa::zone::sim::Zone::Create(
        sa::zone::sim::ZoneConfig{
            .world_id = config->world_id,
            .zone_id = config->zone_id,
            .simulation_rate = config->simulation_rate,
            .command_retry_window = sa::common::protocol::kCommandRetryWindow},
        std::move(world->map), std::move(world->rules), std::move(world->content),
        sa::zone::sim::Rng{seed});
    if (!created) {
        spdlog::critical("zone: {}", created.error());
        return EXIT_FAILURE;
    }
    sa::zone::sim::Zone& zone = *created;

    asio::io_context io;

    // This process generation. The Backend grants ownership to it only while the lease lives.
    const sa::common::InstanceId zone_instance = sa::common::InstanceId::Generate();
    spdlog::info("zone_instance_id={}", zone_instance.ToHex());

    auto backend_channel =
        grpc::CreateChannel(config->backend_address, grpc::InsecureChannelCredentials());
    sa::zone::ZoneLeaseRenewer renewer{io,
                                       backend_channel,
                                       config->backend_rpc_timeout,
                                       config->world_id,
                                       config->zone_id,
                                       zone_instance};
    const sa::zone::BackendCallConfig backend_calls{.rpc_timeout = config->backend_rpc_timeout,
                                                    .call_attempts = config->backend_call_attempts,
                                                    .retry_delay = config->backend_retry_delay};
    sa::zone::EconomyClient economy{io, backend_channel, backend_calls};
    sa::zone::DirectoryClient directory{io, backend_channel, backend_calls};
    sa::common::LeaseKeeper lease{
        io, "zone",
        sa::common::LeaseKeeperConfig{.renew_interval = config->lease_renew_interval,
                                      .safety_margin = config->lease_safety_margin},
        [&renewer](sa::common::LeaseKeeper::Done done) { renewer(std::move(done)); }};

    sa::zone::ZoneHost host{
        zone,
        sa::zone::ZoneHost::Identity{.zone_instance = zone_instance,
                                     .simulation_hz = config->simulation_rate.hz,
                                     .ticks_per_snapshot = config->TicksPerSnapshot(),
                                     .ticks_per_checkpoint = config->TicksPerCheckpoint(),
                                     .max_sessions = config->max_sessions},
        economy, directory, [&lease] { return lease.IsAlive(); }};
    lease.set_on_lost([&host] { host.EvictAll(sa::internal::v1::EVICT_REASON_LEASE_LOST); });

    sa::zone::GatewayLinkServer link_server{
        io,
        sa::zone::GatewayLinkServerConfig{.listen_address = config->link_listen_address,
                                          .listen_port = config->link_listen_port,
                                          .max_gateways = config->link_max_gateways,
                                          .frame_limits = config->link_frame_limits,
                                          .close_linger = config->link_close_linger},
        host};
    host.set_on_link_closed(
        [&link_server](sa::zone::GatewayLink& link) { link_server.Forget(link); });

    std::optional<sa::zone::LocalBotHost> bots;
    std::optional<BotStatsLogger> bot_stats;
    if (config->dev_bots.count > 0) {
        bots.emplace(zone, config->dev_bots, config->TicksPerSnapshot(), seed);
        bot_stats.emplace(io, *bots, config->stats_log_interval);
        spdlog::warn("dev mode: {} in-process bots, no network", bots->size());
    }

    sa::zone::TickRunner runner{
        io, zone,
        sa::zone::sim::FixedStepScheduler{config->simulation_rate, config->max_catch_up_ticks},
        config->stats_log_interval, [&](sa::common::TickIndex tick) {
            if (bots) {
                bots->OnTick(tick);
            }
            host.OnTick(tick);
        }};

    // Shutdown: evict sessions (gateways kick their clients), stop the simulation, wait the grace
    // period for the eviction frames, then close the gateway links. The io_context is not
    // stopped: io.run() returns once the links have closed (each bounded by a linger) and every
    // economy call has answered, retries included (each bounded by its deadline and attempt
    // count). A kill or pickup in flight still settles, and no late result is posted into a
    // destroyed io_context.
    asio::steady_timer drain_deadline{io};
    asio::signal_set signals{io, SIGINT, SIGTERM};
    signals.async_wait([&](const std::error_code&, int signal) {
        spdlog::info("signal {} received, draining for {} ms", signal,
                     config->shutdown_grace.count());
        host.EvictAll(sa::internal::v1::EVICT_REASON_ZONE_SHUTDOWN);
        runner.Stop();
        lease.Stop();
        if (bot_stats) {
            bot_stats->Stop();
        }
        drain_deadline.expires_after(config->shutdown_grace);
        drain_deadline.async_wait([&](const std::error_code&) { link_server.Stop(); });
    });

    lease.Start();
    link_server.Start();
    runner.Start();
    if (bot_stats) {
        bot_stats->Start();
    }
    io.run();
    spdlog::info("sessions attached={} detached={} evicted={} snapshots={}", host.stats().attaches,
                 host.stats().detaches, host.stats().evictions, host.stats().snapshots_sent);
    spdlog::info(
        "economy kills_recorded={} kills_refused={} pickups_acquired={} "
        "pickups_refused={} backend_failures={}",
        host.stats().kills_recorded, host.stats().kills_refused, host.stats().pickups_acquired,
        host.stats().pickups_refused, host.stats().backend_failures);
    spdlog::info(
        "placement checkpoints_saved={} checkpoints_refused={} transfers_completed={} "
        "transfers_refused={} stale_attaches={} superseded_sessions={}",
        host.stats().checkpoints_saved, host.stats().checkpoints_refused,
        host.stats().transfers_completed, host.stats().transfers_refused,
        host.stats().stale_attaches, host.stats().superseded_sessions);
    spdlog::info("stopped at tick={}", zone.current_tick().value());
    return EXIT_SUCCESS;
}

}  // namespace
