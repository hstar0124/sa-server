#pragma once
// Shared fixtures for the ZoneHost tests: a recording link sink, a scripted Backend, a small zone
// with one rat and helpers that build link messages.

#include "sa/common/instance_id.hpp"
#include "sa/common/protocol_limits.hpp"
#include "sa/zone/sim/zone.hpp"

#include "directory_client.hpp"
#include "economy_client.hpp"
#include "trade_client.hpp"
#include "zone_host.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sa::zone::host_fixture {

using internal::v1::ZoneToGateway;

class FakeSink final : public LinkSink {
public:
    void Send(const ZoneToGateway& message) override { sent.push_back(message); }
    void SendPayload(std::span<const std::byte> payload) override {
        ZoneToGateway message;
        if (!message.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
            throw std::logic_error{"the host sent a payload that is not a ZoneToGateway"};
        }
        sent.push_back(std::move(message));
    }
    [[nodiscard]] std::size_t QueuedBytes() const override { return queued; }

    [[nodiscard]] std::vector<ZoneToGateway> OfCase(ZoneToGateway::BodyCase c) const {
        std::vector<ZoneToGateway> out;
        for (const auto& m : sent) {
            if (m.body_case() == c) {
                out.push_back(m);
            }
        }
        return out;
    }

    std::vector<ZoneToGateway> sent;
    std::size_t queued = 0;  // what the link reports as unsent; tests raise it to congest
};

// A scripted Backend: requests are captured, the test answers them when it chooses.
class FakeEconomy final : public EconomyGateway {
public:
    void RecordKill(backend::v1::RecordKillRequest request,
                    Reply<backend::v1::RecordKillResponse> reply) override {
        kills.push_back(std::move(request));
        kill_replies.push_back(std::move(reply));
    }
    void PickupItem(backend::v1::PickupItemRequest request,
                    Reply<backend::v1::PickupItemResponse> reply) override {
        pickups.push_back(std::move(request));
        pickup_replies.push_back(std::move(reply));
    }
    void GetCharacterAssets(backend::v1::GetCharacterAssetsRequest request,
                            Reply<backend::v1::GetCharacterAssetsResponse> reply) override {
        asset_requests.push_back(std::move(request));
        asset_replies.push_back(std::move(reply));
    }

    std::vector<backend::v1::RecordKillRequest> kills;
    std::vector<Reply<backend::v1::RecordKillResponse>> kill_replies;
    std::vector<backend::v1::PickupItemRequest> pickups;
    std::vector<Reply<backend::v1::PickupItemResponse>> pickup_replies;
    std::vector<backend::v1::GetCharacterAssetsRequest> asset_requests;
    std::vector<Reply<backend::v1::GetCharacterAssetsResponse>> asset_replies;
};

// A scripted WorldDirectory: requests are captured, the test answers them when it chooses.
class FakeDirectory final : public DirectoryGateway {
public:
    void SaveCheckpoint(backend::v1::SaveCheckpointRequest request,
                        Reply<backend::v1::SaveCheckpointResponse> reply) override {
        checkpoints.push_back(std::move(request));
        checkpoint_replies.push_back(std::move(reply));
    }
    void TransferCharacter(backend::v1::TransferCharacterRequest request,
                           Reply<backend::v1::TransferCharacterResponse> reply) override {
        transfers.push_back(std::move(request));
        transfer_replies.push_back(std::move(reply));
    }

    std::vector<backend::v1::SaveCheckpointRequest> checkpoints;
    std::vector<Reply<backend::v1::SaveCheckpointResponse>> checkpoint_replies;
    std::vector<backend::v1::TransferCharacterRequest> transfers;
    std::vector<Reply<backend::v1::TransferCharacterResponse>> transfer_replies;
};

// Records every trade step; a test answers it through the matching reply.
class FakeTrades final : public TradeGateway {
public:
    void OpenTrade(backend::v1::OpenTradeRequest request,
                   Reply<backend::v1::TradeResponse> reply) override {
        opens.push_back(std::move(request));
        replies.push_back(std::move(reply));
    }
    void OfferTrade(backend::v1::OfferTradeRequest request,
                    Reply<backend::v1::TradeResponse> reply) override {
        offers.push_back(std::move(request));
        replies.push_back(std::move(reply));
    }
    void AcceptTrade(backend::v1::AcceptTradeRequest request,
                     Reply<backend::v1::TradeResponse> reply) override {
        accepts.push_back(std::move(request));
        replies.push_back(std::move(reply));
    }
    void CancelTrade(backend::v1::CancelTradeRequest request,
                     Reply<backend::v1::TradeResponse> reply) override {
        cancels.push_back(std::move(request));
        replies.push_back(std::move(reply));
    }

    std::vector<backend::v1::OpenTradeRequest> opens;
    std::vector<backend::v1::OfferTradeRequest> offers;
    std::vector<backend::v1::AcceptTradeRequest> accepts;
    std::vector<backend::v1::CancelTradeRequest> cancels;
    std::vector<Reply<backend::v1::TradeResponse>> replies;  // in call order
};

constexpr sim::MonsterTemplateId kRat{7};
constexpr sim::ItemTemplateId kGel{2001};
constexpr std::uint32_t kInventoryCapacity = 20;

inline sim::ContentBundle TestContent() {
    sim::ContentBundle c;
    c.version = "test";
    c.items[kGel] = sim::ItemDefinition{.id = kGel, .name = "gel"};
    c.monsters[kRat] = sim::MonsterDefinition{.id = kRat,
                                              .name = "rat",
                                              .max_hp = 20,
                                              .speed_mps = 0.0F,
                                              .aggro_radius_m = 0.0F,
                                              .leash_radius_m = 1.0F,
                                              .attack_range_m = 1.0F,
                                              .attack_damage = 0,
                                              .attack_cooldown = std::chrono::seconds{1},
                                              .respawn = std::chrono::hours{1}};
    c.drops[kRat] = sim::DropTable{
        .monster = kRat, .gold = 5, .entries = {sim::DropEntry{.item = kGel, .chance = 1.0F}}};
    c.skills[sim::SkillId{1}] = sim::SkillDefinition{.id = sim::SkillId{1},
                                                     .name = "strike",
                                                     .kind = sim::SkillKind::kTargeted,
                                                     .cooldown = std::chrono::milliseconds{500},
                                                     .range_m = 2.5F,
                                                     .damage = 20};
    c.classes[sim::ClassId{1}] = sim::ClassDefinition{.id = sim::ClassId{1},
                                                      .name = "w",
                                                      .max_hp = 100,
                                                      .max_mp = 10,
                                                      .skills = {sim::SkillId{1}}};
    return c;
}

// Players spawn at (50,50); a rat stands 2 m away, inside strike range. A portal to zone 2 stands
// 10 m to the west.
constexpr common::Vec2 kSpawn{50.0F, 50.0F};
constexpr common::Vec2 kRatHome{52.0F, 50.0F};
constexpr sim::PortalId kGate{1};
constexpr common::Vec2 kGatePosition{40.0F, 50.0F};
constexpr common::ZoneId kDestination{2};
constexpr common::Vec2 kArrival{8.0F, 90.0F};

inline sim::Zone MakeZone() {
    auto zone = sim::Zone::Create(
        sim::ZoneConfig{.world_id = common::WorldId{1},
                        .zone_id = common::ZoneId{1},
                        .simulation_rate = sim::TickRate{.hz = 10},
                        .command_retry_window = common::protocol::kCommandRetryWindow,
                        .ticks_per_view = 2},  // Identity().ticks_per_snapshot
        sim::MapData{.size = {100.0F, 100.0F},
                     .cell_size = 10.0F,
                     .obstacles = {},
                     .spawn_points = {kSpawn},
                     .monster_spawns = {sim::MonsterSpawn{
                         .template_id = kRat, .position = kRatHome, .count = 1}},
                     .portals = {sim::Portal{.id = kGate,
                                             .position = kGatePosition,
                                             .radius_m = 3.0F,
                                             .to_zone = kDestination,
                                             .arrival = kArrival}}},
        sim::ZoneRules{.movement = sim::MovementRules{.max_speed_mps = 10.0F,
                                                      .body_radius_m = 0.4F,
                                                      .aoi_enter_radius_m = 20.0F,
                                                      .aoi_leave_radius_m = 25.0F,
                                                      .aoi_max_entities = 64,
                                                      .aoi_max_spawns_per_view = 64,
                                                      .publish_velocity_delta_mps = 0.25F,
                                                      .publish_position_error_m = 0.1F,
                                                      .commands_per_tick_budget = 100,
                                                      .max_queued_per_player = 8},
                       .combat = sim::CombatRules{.monster_think_hz = 5,
                                                  .player_respawn = std::chrono::seconds{1},
                                                  .monster_stall_timeout = std::chrono::seconds{1},
                                                  .monster_stall_distance_m = 1.0F,
                                                  .range_tolerance = 1.1F,
                                                  .monster_home_arrive_m = 0.5F,
                                                  .monster_pack_ring_radius_m = 1.5F},
                       .economy = sim::EconomyRules{.inventory_capacity = kInventoryCapacity,
                                                    .pickup_range_m = 2.0F,
                                                    .trade_range_m = 5.0F,
                                                    .ground_item_despawn = std::chrono::seconds{60},
                                                    .primary_currency = "gold"}},
        TestContent(), sim::Rng{1});
    if (!zone) {
        throw std::logic_error{zone.error()};
    }
    return std::move(*zone);
}

// Session outputs of one client message kind, in order.
template <typename Pick>
std::vector<game::v1::ServerMessage> OutputsWhere(const FakeSink& sink, Pick pick) {
    std::vector<game::v1::ServerMessage> out;
    for (const auto& m : sink.sent) {
        if (m.body_case() == ZoneToGateway::kSessionOutput && pick(m.session_output().message())) {
            out.push_back(m.session_output().message());
        }
    }
    return out;
}

inline backend::v1::GetCharacterAssetsResponse AssetsOf(std::uint64_t gold) {
    backend::v1::GetCharacterAssetsResponse response;
    response.set_inventory_capacity(kInventoryCapacity);
    response.set_inventory_version(0);
    auto* wallet = response.add_wallet();
    wallet->set_currency("gold");
    wallet->set_balance(gold);
    return response;
}

// Larger than any test snapshot, so a sink reporting nothing queued takes every session.
inline constexpr std::size_t kTestSnapshotBudget = std::size_t{64} * 1024;

inline ZoneHost::Identity Identity(std::uint32_t max_sessions = 10) {
    return ZoneHost::Identity{.zone_instance = common::InstanceId::Generate(),
                              .simulation_hz = 10,
                              .ticks_per_snapshot = 2,
                              .snapshot_budget_bytes = kTestSnapshotBudget,
                              .ticks_per_checkpoint = 4,
                              .max_sessions = max_sessions};
}

// Gateway session ids are 16-byte instance ids on the wire. Tests name sessions ("s1") and this
// turns a name into a valid, distinct id.
inline std::string SessionWire(std::string_view name) {
    std::string wire(common::InstanceId::kBytes, '\0');
    std::ranges::copy(name.substr(0, wire.size()), wire.begin());
    return wire;
}

inline internal::v1::SessionAttach AttachFor(const std::string& session, std::uint64_t character) {
    internal::v1::SessionAttach a;
    a.set_gateway_session_id(SessionWire(session));
    a.set_character_id(character);
    a.set_class_id(1);
    a.set_owner_epoch(1);
    return a;
}

// The one message of the given case sent to the sink; anything else is a test bug.
inline const ZoneToGateway& Only(const FakeSink& sink, ZoneToGateway::BodyCase c) {
    const auto matches = sink.OfCase(c);
    if (matches.size() != 1) {
        throw std::logic_error{"expected exactly one message of that case"};
    }
    static ZoneToGateway copy;
    copy = matches.front();
    return copy;
}

// ---- economy ---------------------------------------------------------------------------------

// Exactly one of something; anything else is a test bug rather than an assertion to report.
template <typename T>
T Single(std::vector<T> items, const char* what) {
    if (items.size() != 1) {
        throw std::logic_error{std::string{"expected exactly one "} + what};
    }
    return std::move(items.front());
}

inline std::vector<game::v1::InventoryUpdate> InventoryUpdates(const FakeSink& sink) {
    std::vector<game::v1::InventoryUpdate> out;
    for (const auto& m : OutputsWhere(
             sink, [](const game::v1::ServerMessage& m) { return m.has_inventory_update(); })) {
        out.push_back(m.inventory_update());
    }
    return out;
}

inline std::vector<game::v1::CommandResult> CommandResults(const FakeSink& sink) {
    std::vector<game::v1::CommandResult> out;
    for (const auto& m : OutputsWhere(
             sink, [](const game::v1::ServerMessage& m) { return m.has_command_result(); })) {
        out.push_back(m.command_result());
    }
    return out;
}

inline internal::v1::SessionInput StrikeInput(const std::string& session, common::EntityId target,
                                              std::uint64_t request) {
    internal::v1::SessionInput input;
    input.set_gateway_session_id(SessionWire(session));
    auto* cast = input.mutable_message()->mutable_cast_skill();
    cast->set_request_id(request);
    cast->set_skill_id(1);
    cast->set_target_entity_id(target.value());
    return input;
}

inline internal::v1::SessionInput PickupInput(const std::string& session, common::EntityId item,
                                              std::uint64_t request) {
    internal::v1::SessionInput input;
    input.set_gateway_session_id(SessionWire(session));
    auto* pickup = input.mutable_message()->mutable_pickup_item();
    pickup->set_request_id(request);
    pickup->set_item_entity_id(item.value());
    return input;
}

// Attaches session "s1" (character 42) with known assets and kills the rat standing next to it.
inline void AttachAndKillTheRat(sim::Zone& zone, ZoneHost& host, FakeEconomy& economy,
                                FakeSink& sink) {
    host.Attach(sink, AttachFor("s1", 42));
    Single(economy.asset_replies, "assets request")(AssetsOf(0));
    const common::EntityId rat = Single(zone.LiveMonsters(), "rat");
    host.Input(StrikeInput("s1", rat, 1));  // 20 damage: the rat has 20 hp
    zone.Tick();
    host.OnTick(zone.current_tick());
    if (!zone.LiveMonsters().empty()) {
        throw std::logic_error{"the rat should be dead"};
    }
}

// Puts one committed gel next to the rat's home and returns its entity.
inline common::EntityId GelOnTheGround(sim::Zone& zone, common::ItemInstanceId instance) {
    const sim::GroundItem gel{.instance = instance, .item = kGel, .position = kRatHome};
    return Single(zone.SpawnGroundItems(std::span{&gel, 1}), "spawned gel");
}

// Submits a pickup for session "s1" and runs one tick so the host forwards it.
inline void PickUp(sim::Zone& zone, ZoneHost& host, common::EntityId item, std::uint64_t request) {
    host.Input(PickupInput("s1", item, request));
    zone.Tick();
    host.OnTick(zone.current_tick());
}

}  // namespace sa::zone::host_fixture
