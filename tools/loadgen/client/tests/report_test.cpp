#include "report.hpp"

#include <gtest/gtest.h>

namespace sa::loadgen {
namespace {

BotReport Good(std::uint32_t index, std::size_t players_seen = 1) {
    BotReport b;
    b.index = index;
    b.provisioned = b.connected = b.welcomed = true;
    b.max_visible = players_seen;
    b.max_visible_players = players_seen;
    b.snapshots = 10;
    b.last_acked_input = 5;
    return b;
}

TEST(RunReport, PassesWhenEveryoneEnteredAndSawSomeone) {
    const RunReport report =
        RunReport::Aggregate({Good(0), Good(1)}, {.min_bots_seeing_others = 2, .min_kills = 0});
    EXPECT_TRUE(report.Succeeded());
    EXPECT_EQ(report.welcomed, 2U);
    EXPECT_EQ(report.saw_another_player, 2U);
}

TEST(RunReport, ThresholdZeroDoesNotRequireAnyMeeting) {
    EXPECT_TRUE(RunReport::Aggregate({Good(0, 0)}, {.min_bots_seeing_others = 0, .min_kills = 0})
                    .Succeeded());
    EXPECT_TRUE(RunReport::Aggregate({Good(0, 0), Good(1, 1)},
                                     {.min_bots_seeing_others = 1, .min_kills = 0})
                    .Succeeded());
}

TEST(RunReport, RequiresTheConfiguredKills) {
    BotReport slayer = Good(0);
    slayer.combat.kills = 2;
    slayer.combat.hits_dealt = 5;
    const RunReport enough =
        RunReport::Aggregate({slayer, Good(1)}, {.min_bots_seeing_others = 1, .min_kills = 2});
    EXPECT_TRUE(enough.Succeeded());
    EXPECT_EQ(enough.kills, 2U);
    EXPECT_FALSE(
        RunReport::Aggregate({slayer, Good(1)}, {.min_bots_seeing_others = 1, .min_kills = 3})
            .Succeeded());
}

TEST(RunReport, FailsOnKickFailureIsolationOrIntegrity) {
    BotReport kicked = Good(1);
    kicked.kicked = game::v1::KICK_REASON_INVALID_TICKET;
    EXPECT_FALSE(
        RunReport::Aggregate({Good(0), kicked}, {.min_bots_seeing_others = 1, .min_kills = 0})
            .Succeeded());

    BotReport failed = Good(1);
    failed.failure = "connect: refused";
    EXPECT_FALSE(
        RunReport::Aggregate({Good(0), failed}, {.min_bots_seeing_others = 1, .min_kills = 0})
            .Succeeded());

    EXPECT_FALSE(
        RunReport::Aggregate({Good(0), Good(1, 0)}, {.min_bots_seeing_others = 2, .min_kills = 0})
            .Succeeded())
        << "threshold 2, one bot saw nobody";

    BotReport inconsistent = Good(1);
    inconsistent.integrity.despawn_of_unknown = 1;
    const RunReport r = RunReport::Aggregate({Good(0), inconsistent},
                                             {.min_bots_seeing_others = 1, .min_kills = 0});
    EXPECT_FALSE(r.Succeeded());
    EXPECT_EQ(r.integrity_errors, 1U);
    EXPECT_NE(r.ToText().find("RESULT: FAIL"), std::string::npos);
}

TEST(RunReport, FailsWhenABotsMovementWasNeverProcessed) {
    BotReport frozen = Good(1);
    frozen.last_acked_input = 0;
    const RunReport report =
        RunReport::Aggregate({Good(0), frozen}, {.min_bots_seeing_others = 0, .min_kills = 0});
    EXPECT_FALSE(report.Succeeded()) << "entered, but the zone never applied its input";
    EXPECT_EQ(report.inputs_acked, 1U);
}

TEST(RunReport, MonstersAndItemsDoNotCountAsMeetingAnotherPlayer) {
    BotReport sees_a_slime = Good(0, 0);
    sees_a_slime.max_visible = 3;
    const RunReport report =
        RunReport::Aggregate({sees_a_slime}, {.min_bots_seeing_others = 1, .min_kills = 0});
    EXPECT_EQ(report.saw_another_player, 0U);
    EXPECT_FALSE(report.Succeeded());
}

TEST(RunReport, RequiresTheConfiguredPickupsAndCountsThemAcrossBots) {
    BotReport looter = Good(0);
    looter.pickups_sent = 4;
    looter.loot.pickups_acquired = 2;
    looter.loot.pickups_rejected = 1;
    looter.loot.pickups_unknown = 1;
    looter.loot.items_held = 2;
    looter.loot.gold = 7;
    BotReport other = Good(1);
    other.loot.pickups_acquired = 1;
    const RunReport enough =
        RunReport::Aggregate({looter, other}, {.min_bots_seeing_others = 1, .min_pickups = 3});
    EXPECT_TRUE(enough.Succeeded());
    EXPECT_EQ(enough.pickups_acquired, 3U);
    EXPECT_EQ(enough.pickups_rejected, 1U);
    EXPECT_EQ(enough.pickups_unknown, 1U);
    EXPECT_EQ(enough.gold, 7U);
    EXPECT_NE(enough.ToText().find("acquired=3 (required 3)"), std::string::npos);
    EXPECT_FALSE(
        RunReport::Aggregate({looter, other}, {.min_bots_seeing_others = 1, .min_pickups = 4})
            .Succeeded());
}

TEST(RunReport, RequiresTheConfiguredZoneChanges) {
    BotReport traveler = Good(0);
    traveler.transfers_sent = 3;
    traveler.travel.transfers_accepted = 2;
    traveler.travel.transfers_unknown = 1;
    traveler.travel.zone_changes = 2;
    const RunReport short_of_it =
        RunReport::Aggregate({traveler}, {.min_bots_seeing_others = 0, .min_zone_changes = 3});
    EXPECT_FALSE(short_of_it.Succeeded());
    EXPECT_NE(short_of_it.ToText().find("zone changes=2 (required 3)"), std::string::npos);
    EXPECT_TRUE(
        RunReport::Aggregate({traveler}, {.min_bots_seeing_others = 0, .min_zone_changes = 2})
            .Succeeded());
}

}  // namespace
}  // namespace sa::loadgen
