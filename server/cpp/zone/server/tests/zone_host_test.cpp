#include "zone_host.hpp"

#include "sa/common/instance_id.hpp"
#include "sa/zone/sim/zone.hpp"

#include "economy_client.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sa::zone {
namespace {

using internal::v1::ZoneToGateway;

class FakeSink final : public LinkSink {
public:
    void Send(const ZoneToGateway& message) override { sent.push_back(message); }

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

constexpr sim::MonsterTemplateId kRat{7};
constexpr sim::ItemTemplateId kGel{2001};
constexpr std::uint32_t kInventoryCapacity = 20;

sim::ContentBundle TestContent() {
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

// Players spawn at (50,50); a rat stands 2 m away, inside strike range.
constexpr common::Vec2 kSpawn{50.0F, 50.0F};
constexpr common::Vec2 kRatHome{52.0F, 50.0F};

sim::Zone MakeZone() {
    auto zone = sim::Zone::Create(
        sim::ZoneConfig{.world_id = common::WorldId{1},
                        .zone_id = common::ZoneId{1},
                        .simulation_rate = sim::TickRate{.hz = 10}},
        sim::MapData{.size = {100.0F, 100.0F},
                     .cell_size = 10.0F,
                     .obstacles = {},
                     .spawn_points = {kSpawn},
                     .monster_spawns = {sim::MonsterSpawn{
                         .template_id = kRat, .position = kRatHome, .count = 1}}},
        sim::ZoneRules{.movement = sim::MovementRules{.max_speed_mps = 10.0F,
                                                      .aoi_enter_radius_m = 20.0F,
                                                      .aoi_leave_radius_m = 25.0F,
                                                      .commands_per_tick_budget = 100},
                       .combat = sim::CombatRules{.monster_think_hz = 5,
                                                  .player_respawn = std::chrono::seconds{1}},
                       .economy = sim::EconomyRules{.inventory_capacity = kInventoryCapacity,
                                                    .pickup_range_m = 2.0F,
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

backend::v1::GetCharacterAssetsResponse AssetsOf(std::uint64_t gold) {
    backend::v1::GetCharacterAssetsResponse response;
    response.set_inventory_capacity(kInventoryCapacity);
    response.set_inventory_version(0);
    auto* wallet = response.add_wallet();
    wallet->set_currency("gold");
    wallet->set_balance(gold);
    return response;
}

ZoneHost::Identity Identity(std::uint32_t max_sessions = 10) {
    return ZoneHost::Identity{.zone_instance = common::InstanceId::Generate(),
                              .simulation_hz = 10,
                              .ticks_per_snapshot = 2,
                              .max_sessions = max_sessions};
}

internal::v1::SessionAttach AttachFor(const std::string& session, std::uint64_t character) {
    internal::v1::SessionAttach a;
    a.set_gateway_session_id(session);
    a.set_character_id(character);
    a.set_class_id(1);
    a.set_owner_epoch(1);
    return a;
}

// The one message of the given case sent to the sink; anything else is a test bug.
const ZoneToGateway& Only(const FakeSink& sink, ZoneToGateway::BodyCase c) {
    const auto matches = sink.OfCase(c);
    if (matches.size() != 1) {
        throw std::logic_error{"expected exactly one message of that case"};
    }
    static ZoneToGateway copy;
    copy = matches.front();
    return copy;
}

TEST(ZoneHost, AttachSpawnsAPlayerAndAcknowledgesTheSession) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink sink;

    host.Attach(sink, AttachFor("s1", 42));

    EXPECT_EQ(host.session_count(), 1U);
    EXPECT_EQ(zone.stats().players, 1U);
    EXPECT_EQ(Only(sink, ZoneToGateway::kSessionAttached).session_attached().gateway_session_id(),
              "s1");
}

TEST(ZoneHost, AttachWelcomesTheClientWithItsOwnEntityId) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink sink;

    host.Attach(sink, AttachFor("s1", 42));

    const std::uint64_t entity =
        Only(sink, ZoneToGateway::kSessionAttached).session_attached().entity_id();
    const auto& welcome =
        Only(sink, ZoneToGateway::kSessionOutput).session_output().message().welcome();
    EXPECT_EQ(welcome.character_id(), 42U);
    EXPECT_EQ(welcome.entity_id(), entity);
}

TEST(ZoneHost, RefusesDuplicatesFullZoneAndWhenNotAuthoritative) {
    sim::Zone zone = MakeZone();
    bool authoritative = true;
    FakeEconomy economy;
    ZoneHost host{zone, Identity(1), economy, [&] { return authoritative; }};
    FakeSink sink;

    host.Attach(sink, AttachFor("s1", 1));
    host.Attach(sink, AttachFor("s1", 1));
    host.Attach(sink, AttachFor("s2", 2));
    authoritative = false;
    host.Attach(sink, AttachFor("s3", 3));

    const auto rejected = sink.OfCase(ZoneToGateway::kSessionRejected);
    ASSERT_EQ(rejected.size(), 3U);
    EXPECT_EQ(rejected[0].session_rejected().reason(),
              internal::v1::REJECT_REASON_DUPLICATE_SESSION);
    EXPECT_EQ(rejected[1].session_rejected().reason(), internal::v1::REJECT_REASON_ZONE_FULL);
    EXPECT_EQ(rejected[2].session_rejected().reason(),
              internal::v1::REJECT_REASON_NOT_AUTHORITATIVE);
    EXPECT_EQ(host.session_count(), 1U);
}

TEST(ZoneHost, RefusesACharacterWhoseClassIsNotInTheContent) {
    constexpr std::uint32_t kClassNotInContent = 999;
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink sink;

    auto attach = AttachFor("s1", 1);
    attach.set_class_id(kClassNotInContent);
    host.Attach(sink, attach);

    EXPECT_EQ(Only(sink, ZoneToGateway::kSessionRejected).session_rejected().reason(),
              internal::v1::REJECT_REASON_UNKNOWN_CLASS);
    EXPECT_TRUE(sink.OfCase(ZoneToGateway::kSessionAttached).empty());
    EXPECT_EQ(host.session_count(), 0U);
    EXPECT_EQ(zone.stats().players, 0U);
}

TEST(ZoneHost, InputReachesTheSimulationAndSnapshotsFollowCadence) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink sink;
    host.Attach(sink, AttachFor("s1", 1));
    sink.sent.clear();

    internal::v1::SessionInput input;
    input.set_gateway_session_id("s1");
    input.mutable_message()->mutable_move_input()->set_input_seq(1);
    input.mutable_message()->mutable_move_input()->mutable_direction()->set_x(1.0F);
    host.Input(input);
    zone.Tick();
    host.OnTick(zone.current_tick());  // tick 1: no snapshot (cadence 2)
    zone.Tick();
    host.OnTick(zone.current_tick());  // tick 2: snapshot

    const auto outputs = sink.OfCase(ZoneToGateway::kSessionOutput);
    ASSERT_EQ(outputs.size(), 1U);
    const auto& snapshot = outputs[0].session_output().message().world_snapshot();
    EXPECT_EQ(snapshot.server_tick(), 2U);
    EXPECT_EQ(snapshot.last_processed_input_seq(), 1U);
    EXPECT_GT(snapshot.self().position().x(), 50.0F) << "moved right for two ticks";
    EXPECT_EQ(host.stats().inputs, 1U);
}

// First snapshot delivered to a sink; a missing snapshot is a test bug.
const game::v1::WorldSnapshot& FirstSnapshotTo(const FakeSink& sink) {
    for (const auto& m : sink.sent) {
        if (m.body_case() == ZoneToGateway::kSessionOutput &&
            m.session_output().message().has_world_snapshot()) {
            return m.session_output().message().world_snapshot();
        }
    }
    throw std::logic_error{"no snapshot was sent"};
}

void TickAndSnapshot(sim::Zone& zone, ZoneHost& host) {
    zone.Tick();
    zone.Tick();
    host.OnTick(zone.current_tick());
}

TEST(ZoneHost, SessionsOnDifferentGatewaysSeeEachOther) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink gateway_a;
    FakeSink gateway_b;
    host.Attach(gateway_a, AttachFor("a", 1));
    host.Attach(gateway_b, AttachFor("b", 2));
    gateway_a.sent.clear();

    TickAndSnapshot(zone, host);

    const game::v1::WorldSnapshot& first = FirstSnapshotTo(gateway_a);
    EXPECT_EQ(first.spawned_size(), 2) << "a sees b and the rat";
    EXPECT_TRUE(std::ranges::any_of(first.spawned(), [](const game::v1::EntitySpawn& e) {
        return e.kind() == game::v1::ENTITY_KIND_PLAYER;
    })) << "a sees b";
}

TEST(ZoneHost, DetachDespawnsAndObserversAreTold) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink gateway_a;
    FakeSink gateway_b;
    host.Attach(gateway_a, AttachFor("a", 1));
    host.Attach(gateway_b, AttachFor("b", 2));
    TickAndSnapshot(zone, host);

    internal::v1::SessionDetach detach;
    detach.set_gateway_session_id("b");
    host.Detach(detach);
    gateway_a.sent.clear();
    TickAndSnapshot(zone, host);

    EXPECT_EQ(FirstSnapshotTo(gateway_a).despawned_size(), 1) << "a is told b left";
    EXPECT_EQ(zone.stats().players, 1U);
    EXPECT_EQ(host.stats().detaches, 1U);
}

TEST(ZoneHost, CastSkillYieldsACommandResultOnTheNextTick) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink sink;
    host.Attach(sink, AttachFor("s1", 1));
    sink.sent.clear();

    internal::v1::SessionInput input;
    input.set_gateway_session_id("s1");
    input.mutable_message()->mutable_cast_skill()->set_request_id(1);
    input.mutable_message()->mutable_cast_skill()->set_skill_id(1);
    input.mutable_message()->mutable_cast_skill()->set_target_entity_id(4242);  // no such monster
    host.Input(input);
    zone.Tick();
    host.OnTick(zone.current_tick());  // tick 1: events go out even without a snapshot

    const auto& result =
        Only(sink, ZoneToGateway::kSessionOutput).session_output().message().command_result();
    EXPECT_EQ(result.request_id(), 1U);
    EXPECT_EQ(result.status(), game::v1::COMMAND_STATUS_REJECTED);
    EXPECT_EQ(result.reject_code(), game::v1::REJECT_CODE_INVALID_TARGET);
    EXPECT_EQ(host.stats().events_sent, 1U);
}

TEST(ZoneHost, LinkClosureAndEvictionRemoveEveryoneOnThatLink) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink gateway_a;
    FakeSink gateway_b;
    host.Attach(gateway_a, AttachFor("a1", 1));
    host.Attach(gateway_a, AttachFor("a2", 2));
    host.Attach(gateway_b, AttachFor("b1", 3));

    host.LinkClosed(gateway_a);
    EXPECT_EQ(host.session_count(), 1U);
    EXPECT_EQ(zone.stats().players, 1U);

    host.EvictAll(internal::v1::EVICT_REASON_LEASE_LOST);
    EXPECT_EQ(host.session_count(), 0U);
    const auto evicted = gateway_b.OfCase(ZoneToGateway::kSessionEvicted);
    ASSERT_EQ(evicted.size(), 1U);
    EXPECT_EQ(evicted[0].session_evicted().reason(), internal::v1::EVICT_REASON_LEASE_LOST);
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

std::vector<game::v1::InventoryUpdate> InventoryUpdates(const FakeSink& sink) {
    std::vector<game::v1::InventoryUpdate> out;
    for (const auto& m : OutputsWhere(
             sink, [](const game::v1::ServerMessage& m) { return m.has_inventory_update(); })) {
        out.push_back(m.inventory_update());
    }
    return out;
}

std::vector<game::v1::CommandResult> CommandResults(const FakeSink& sink) {
    std::vector<game::v1::CommandResult> out;
    for (const auto& m : OutputsWhere(
             sink, [](const game::v1::ServerMessage& m) { return m.has_command_result(); })) {
        out.push_back(m.command_result());
    }
    return out;
}

internal::v1::SessionInput StrikeInput(const std::string& session, common::EntityId target,
                                       std::uint64_t request) {
    internal::v1::SessionInput input;
    input.set_gateway_session_id(session);
    auto* cast = input.mutable_message()->mutable_cast_skill();
    cast->set_request_id(request);
    cast->set_skill_id(1);
    cast->set_target_entity_id(target.value());
    return input;
}

internal::v1::SessionInput PickupInput(const std::string& session, common::EntityId item,
                                       std::uint64_t request) {
    internal::v1::SessionInput input;
    input.set_gateway_session_id(session);
    auto* pickup = input.mutable_message()->mutable_pickup_item();
    pickup->set_request_id(request);
    pickup->set_item_entity_id(item.value());
    return input;
}

// Attaches session "s1" (character 42) with known assets and kills the rat standing next to it.
void AttachAndKillTheRat(sim::Zone& zone, ZoneHost& host, FakeEconomy& economy, FakeSink& sink) {
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
common::EntityId GelOnTheGround(sim::Zone& zone, common::ItemInstanceId instance) {
    const sim::GroundItem gel{.instance = instance, .item = kGel, .position = kRatHome};
    return Single(zone.SpawnGroundItems(std::span{&gel, 1}), "spawned gel");
}

// Submits a pickup for session "s1" and runs one tick so the host forwards it.
void PickUp(sim::Zone& zone, ZoneHost& host, common::EntityId item, std::uint64_t request) {
    host.Input(PickupInput("s1", item, request));
    zone.Tick();
    host.OnTick(zone.current_tick());
}

TEST(ZoneHostEconomy, AttachAsksTheBackendForAssetsAndForwardsThemAsAnInventoryUpdate) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink sink;

    host.Attach(sink, AttachFor("s1", 42));
    const auto request = Single(economy.asset_requests, "assets request");
    EXPECT_EQ(request.character_id(), 42U);
    EXPECT_EQ(request.world_id(), 1U);
    Single(economy.asset_replies, "assets reply")(AssetsOf(17));

    const game::v1::InventoryUpdate update = Single(InventoryUpdates(sink), "inventory update");
    EXPECT_EQ(update.gold(), 17U);
    EXPECT_EQ(update.capacity(), kInventoryCapacity);
    EXPECT_EQ(host.stats().inventory_updates_sent, 1U);
}

TEST(ZoneHostEconomy, AKillIsRecordedUnderTheSessionsAuthority) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    const ZoneHost::Identity identity = Identity();
    ZoneHost host{zone, identity, economy, [] { return true; }};
    FakeSink sink;

    AttachAndKillTheRat(zone, host, economy, sink);

    const backend::v1::RecordKillRequest kill = Single(economy.kills, "kill record");
    EXPECT_EQ(kill.kill_id().size(), common::KillId::kBytes);
    EXPECT_EQ(kill.killer_character_id(), 42U);
    EXPECT_EQ(kill.monster_template_id(), kRat.value());
    EXPECT_EQ(kill.content_version(), "test");
    EXPECT_EQ(kill.authority().zone_instance_id(), identity.zone_instance.ToWire());
    EXPECT_EQ(kill.authority().owner_epoch(), 1U);
    EXPECT_EQ(kill.drops_size(), 1);
    EXPECT_EQ(kill.drops(0).item_template_id(), kGel.value());
    EXPECT_TRUE(zone.GroundItems().empty()) << "nothing on the ground before the commit";
}

TEST(ZoneHostEconomy, ACommittedKillSpawnsTheDropsAndCreditsTheKiller) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink sink;
    AttachAndKillTheRat(zone, host, economy, sink);
    const backend::v1::RecordKillRequest kill = Single(economy.kills, "kill record");

    backend::v1::RecordKillResponse response;
    response.set_outcome(backend::v1::KILL_OUTCOME_RECORDED);
    response.set_gold_granted(5);
    response.set_gold_balance(5);
    auto* item = response.add_items();
    item->set_item_instance_id(common::ItemInstanceId::Generate().ToWire());
    item->set_item_template_id(kGel.value());
    *item->mutable_position() = kill.drops(0).position();
    Single(economy.kill_replies, "kill reply")(response);

    EXPECT_EQ(zone.GroundItems().size(), 1U);
    EXPECT_EQ(host.stats().kills_recorded, 1U);
    const auto updates = InventoryUpdates(sink);
    EXPECT_EQ(updates.size(), 2U) << "initial assets, then the gold credit";
    EXPECT_EQ(updates.back().gold(), 5U);
}

TEST(ZoneHostEconomy, APickupIsForwardedWithTheItemsInstanceAndAnsweredPending) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink sink;
    AttachAndKillTheRat(zone, host, economy, sink);
    const common::ItemInstanceId instance = common::ItemInstanceId::Generate();
    const common::EntityId item = GelOnTheGround(zone, instance);
    sink.sent.clear();

    PickUp(zone, host, item, 2);

    const backend::v1::PickupItemRequest request = Single(economy.pickups, "pickup request");
    EXPECT_EQ(request.item_instance_id(), instance.ToWire());
    EXPECT_EQ(request.character_id(), 42U);
    EXPECT_EQ(request.request_id().size(), common::ItemInstanceId::kBytes);
    EXPECT_EQ(Single(CommandResults(sink), "pending result").status(),
              game::v1::COMMAND_STATUS_PENDING);
}

TEST(ZoneHostEconomy, AnAcquiredPickupRemovesTheItemAndExtendsTheInventory) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink sink;
    AttachAndKillTheRat(zone, host, economy, sink);
    const common::ItemInstanceId instance = common::ItemInstanceId::Generate();
    const common::EntityId item = GelOnTheGround(zone, instance);
    PickUp(zone, host, item, 2);
    sink.sent.clear();

    backend::v1::PickupItemResponse response;
    response.set_outcome(backend::v1::PICKUP_OUTCOME_ACQUIRED);
    response.set_inventory_version(1);
    response.mutable_item()->set_item_instance_id(instance.ToWire());
    response.mutable_item()->set_item_template_id(kGel.value());
    response.mutable_item()->set_slot_no(0);
    Single(economy.pickup_replies, "pickup reply")(response);
    zone.Tick();
    host.OnTick(zone.current_tick());

    EXPECT_TRUE(zone.GroundItems().empty());
    EXPECT_EQ(host.stats().pickups_acquired, 1U);
    const game::v1::CommandResult result = Single(CommandResults(sink), "final result");
    EXPECT_EQ(result.status(), game::v1::COMMAND_STATUS_ACCEPTED);
    EXPECT_EQ(result.request_id(), 2U);
    const game::v1::InventoryUpdate update = Single(InventoryUpdates(sink), "inventory update");
    EXPECT_EQ(update.items_size(), 1);
    EXPECT_EQ(update.items(0).item_instance_id(), instance.ToWire());
    EXPECT_EQ(update.version(), 1U);
}

TEST(ZoneHostEconomy, ABackendThatDoesNotAnswerReleasesTheItemAndReportsUnknown) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    ZoneHost host{zone, Identity(), economy, [] { return true; }};
    FakeSink sink;
    AttachAndKillTheRat(zone, host, economy, sink);
    const common::EntityId item = GelOnTheGround(zone, common::ItemInstanceId::Generate());
    PickUp(zone, host, item, 2);
    sink.sent.clear();

    Single(economy.pickup_replies, "pickup reply")(std::nullopt);
    zone.Tick();
    host.OnTick(zone.current_tick());

    EXPECT_EQ(zone.GroundItems().size(), 1U) << "still on the ground for a retry";
    EXPECT_EQ(host.stats().backend_failures, 1U);
    const game::v1::CommandResult result = Single(CommandResults(sink), "unknown result");
    EXPECT_EQ(result.status(), game::v1::COMMAND_STATUS_UNKNOWN);
    EXPECT_EQ(result.reject_code(), game::v1::REJECT_CODE_SERVICE_UNAVAILABLE);
}

}  // namespace
}  // namespace sa::zone
