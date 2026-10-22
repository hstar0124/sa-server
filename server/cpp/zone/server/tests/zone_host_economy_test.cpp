#include "zone_host_test_support.hpp"

#include <gtest/gtest.h>

namespace sa::zone {
namespace {

using internal::v1::ZoneToGateway;
using namespace host_fixture;  // NOLINT(google-build-using-namespace): test fixtures only

TEST(ZoneHostEconomy, AttachAsksTheBackendForAssetsAndForwardsThemAsAnInventoryUpdate) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
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
    FakeDirectory directory;
    const ZoneHost::Identity identity = Identity();
    ZoneHost host{zone, identity, economy, directory, [] { return true; }};
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
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
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
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;
    AttachAndKillTheRat(zone, host, economy, sink);
    const common::ItemInstanceId instance = common::ItemInstanceId::Generate();
    const common::EntityId item = GelOnTheGround(zone, instance);
    sink.sent.clear();

    PickUp(zone, host, item, 2);

    const backend::v1::PickupItemRequest request = Single(economy.pickups, "pickup request");
    EXPECT_EQ(request.item_instance_id(), instance.ToWire());
    EXPECT_EQ(request.character_id(), 42U);
    EXPECT_EQ(request.request_id().size(), common::RequestKey::kBytes);
    EXPECT_EQ(Single(CommandResults(sink), "pending result").status(),
              game::v1::COMMAND_STATUS_PENDING);
}

TEST(ZoneHostEconomy, AnAcquiredPickupRemovesTheItemAndExtendsTheInventory) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
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
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
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
    EXPECT_EQ(economy.asset_requests.size(), 2U) << "the assets are read again to settle it";
}

TEST(ZoneHostEconomy, AnUnknownPickupThatDidCommitIsSettledByTheAssetReRead) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;
    AttachAndKillTheRat(zone, host, economy, sink);
    const common::ItemInstanceId instance = common::ItemInstanceId::Generate();
    const common::EntityId item = GelOnTheGround(zone, instance);
    PickUp(zone, host, item, 2);
    Single(economy.pickup_replies, "pickup reply")(std::nullopt);
    sink.sent.clear();

    // The Backend did commit before the answer was lost: the item is in the inventory.
    backend::v1::GetCharacterAssetsResponse assets = AssetsOf(0);
    auto* held = assets.add_items();
    held->set_item_instance_id(instance.ToWire());
    held->set_item_template_id(kGel.value());
    held->set_slot_no(0);
    assets.set_inventory_version(1);
    economy.asset_replies.back()(assets);

    EXPECT_TRUE(zone.GroundItems().empty()) << "no longer shown on the ground";
    const game::v1::InventoryUpdate update = Single(InventoryUpdates(sink), "inventory update");
    ASSERT_EQ(update.items_size(), 1);
    EXPECT_EQ(update.items(0).item_instance_id(), instance.ToWire());
}

TEST(ZoneHostEconomy, AnAnswerThatThisZoneIsNotTheOwnerEvictsTheSession) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;
    AttachAndKillTheRat(zone, host, economy, sink);
    sink.sent.clear();

    backend::v1::RecordKillResponse refused;
    refused.set_outcome(backend::v1::KILL_OUTCOME_STALE_AUTHORITY);
    Single(economy.kill_replies, "kill reply")(refused);

    EXPECT_EQ(Only(sink, ZoneToGateway::kSessionEvicted).session_evicted().reason(),
              internal::v1::EVICT_REASON_NOT_OWNER);
    EXPECT_EQ(host.session_count(), 0U);
    EXPECT_EQ(host.stats().not_owner_evictions, 1U);
    EXPECT_TRUE(zone.GroundItems().empty());
}

TEST(ZoneHostEconomy, AKillTheBackendAlreadyRecordedShowsItsCommittedDrops) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;
    AttachAndKillTheRat(zone, host, economy, sink);
    const backend::v1::RecordKillRequest kill = Single(economy.kills, "kill record");

    backend::v1::RecordKillResponse echoed;
    echoed.set_outcome(backend::v1::KILL_OUTCOME_ALREADY_RECORDED);
    echoed.set_gold_balance(5);
    auto* item = echoed.add_items();
    item->set_item_instance_id(common::ItemInstanceId::Generate().ToWire());
    item->set_item_template_id(kGel.value());
    *item->mutable_position() = kill.drops(0).position();
    Single(economy.kill_replies, "kill reply")(echoed);

    EXPECT_EQ(zone.GroundItems().size(), 1U) << "a retried kill shows the committed drops";
    EXPECT_EQ(host.stats().kills_recorded, 1U);
}

TEST(ZoneHostEconomy, ARefusedKillSpawnsNothing) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;
    AttachAndKillTheRat(zone, host, economy, sink);

    backend::v1::RecordKillResponse refused;
    refused.set_outcome(backend::v1::KILL_OUTCOME_UNKNOWN_TEMPLATE);
    auto* item = refused.add_items();
    item->set_item_instance_id(common::ItemInstanceId::Generate().ToWire());
    item->set_item_template_id(kGel.value());
    Single(economy.kill_replies, "kill reply")(refused);

    EXPECT_TRUE(zone.GroundItems().empty());
    EXPECT_EQ(host.stats().kills_refused, 1U);
    EXPECT_EQ(host.session_count(), 1U) << "only a not-owner answer ends the session";
}

TEST(ZoneHostEconomy, AFailedAssetReadIsRepeatedOnTheNextEconomyAnswer) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;
    host.Attach(sink, AttachFor("s1", 42));
    Single(economy.asset_replies, "assets request")(std::nullopt);
    EXPECT_TRUE(InventoryUpdates(sink).empty());

    const common::EntityId rat = Single(zone.LiveMonsters(), "rat");
    host.Input(StrikeInput("s1", rat, 1));
    zone.Tick();
    host.OnTick(zone.current_tick());
    backend::v1::RecordKillResponse recorded;
    recorded.set_outcome(backend::v1::KILL_OUTCOME_RECORDED);
    recorded.set_gold_balance(5);
    Single(economy.kill_replies, "kill reply")(recorded);

    ASSERT_EQ(economy.asset_requests.size(), 2U) << "the Backend answered, so ask again";
    economy.asset_replies.back()(AssetsOf(5));
    EXPECT_EQ(Single(InventoryUpdates(sink), "inventory update").gold(), 5U);
}

TEST(ZoneHostEconomy, AKillWithNoAnswerReReadsTheKillersAssets) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;
    AttachAndKillTheRat(zone, host, economy, sink);

    Single(economy.kill_replies, "kill reply")(std::nullopt);

    EXPECT_EQ(host.stats().backend_failures, 1U);
    EXPECT_EQ(economy.asset_requests.size(), 2U) << "the gold may have been credited";
}

TEST(ZoneHostEconomy, OutOfOrderPickupAnswersNeverRollTheInventoryBack) {
    sim::Zone zone = MakeZone();
    FakeEconomy economy;
    FakeDirectory directory;
    ZoneHost host{zone, Identity(), economy, directory, [] { return true; }};
    FakeSink sink;
    AttachAndKillTheRat(zone, host, economy, sink);
    const auto first = common::ItemInstanceId::Generate();
    const auto second = common::ItemInstanceId::Generate();
    PickUp(zone, host, GelOnTheGround(zone, first), 2);
    PickUp(zone, host, GelOnTheGround(zone, second), 3);
    ASSERT_EQ(economy.pickup_replies.size(), 2U);
    sink.sent.clear();

    const auto acquired = [](common::ItemInstanceId instance, std::uint32_t slot,
                             std::uint32_t version) {
        backend::v1::PickupItemResponse r;
        r.set_outcome(backend::v1::PICKUP_OUTCOME_ACQUIRED);
        r.set_inventory_version(version);
        r.mutable_item()->set_item_instance_id(instance.ToWire());
        r.mutable_item()->set_item_template_id(kGel.value());
        r.mutable_item()->set_slot_no(slot);
        return r;
    };
    economy.pickup_replies[1](acquired(second, 1, 2));  // committed second, answered first
    economy.pickup_replies[0](acquired(first, 0, 1));

    const auto updates = InventoryUpdates(sink);
    ASSERT_EQ(updates.size(), 2U);
    EXPECT_EQ(updates.back().version(), 2U) << "the older answer does not roll the version back";
    EXPECT_EQ(updates.back().items_size(), 2);
}

}  // namespace
}  // namespace sa::zone
