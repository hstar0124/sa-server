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
    const RunReport report = RunReport::Aggregate({Good(0), Good(1)}, 2, 0);
    EXPECT_TRUE(report.Succeeded());
    EXPECT_EQ(report.welcomed, 2U);
    EXPECT_EQ(report.saw_another_player, 2U);
}

TEST(RunReport, ThresholdZeroDoesNotRequireAnyMeeting) {
    EXPECT_TRUE(RunReport::Aggregate({Good(0, 0)}, 0, 0).Succeeded());
    EXPECT_TRUE(RunReport::Aggregate({Good(0, 0), Good(1, 1)}, 1, 0).Succeeded());
}

TEST(RunReport, RequiresTheConfiguredKills) {
    BotReport slayer = Good(0);
    slayer.combat.kills = 2;
    slayer.combat.hits_dealt = 5;
    const RunReport enough = RunReport::Aggregate({slayer, Good(1)}, 1, 2);
    EXPECT_TRUE(enough.Succeeded());
    EXPECT_EQ(enough.kills, 2U);
    EXPECT_FALSE(RunReport::Aggregate({slayer, Good(1)}, 1, 3).Succeeded());
}

TEST(RunReport, FailsOnKickFailureIsolationOrIntegrity) {
    BotReport kicked = Good(1);
    kicked.kicked = game::v1::KICK_REASON_INVALID_TICKET;
    EXPECT_FALSE(RunReport::Aggregate({Good(0), kicked}, 1, 0).Succeeded());

    BotReport failed = Good(1);
    failed.failure = "connect: refused";
    EXPECT_FALSE(RunReport::Aggregate({Good(0), failed}, 1, 0).Succeeded());

    EXPECT_FALSE(RunReport::Aggregate({Good(0), Good(1, 0)}, 2, 0).Succeeded())
        << "threshold 2, one bot saw nobody";

    BotReport inconsistent = Good(1);
    inconsistent.integrity.despawn_of_unknown = 1;
    const RunReport r = RunReport::Aggregate({Good(0), inconsistent}, 1, 0);
    EXPECT_FALSE(r.Succeeded());
    EXPECT_EQ(r.integrity_errors, 1U);
    EXPECT_NE(r.ToText().find("RESULT: FAIL"), std::string::npos);
}

TEST(RunReport, FailsWhenABotsMovementWasNeverProcessed) {
    BotReport frozen = Good(1);
    frozen.last_acked_input = 0;
    const RunReport report = RunReport::Aggregate({Good(0), frozen}, 0, 0);
    EXPECT_FALSE(report.Succeeded()) << "entered, but the zone never applied its input";
    EXPECT_EQ(report.inputs_acked, 1U);
}

TEST(RunReport, MonstersAndItemsDoNotCountAsMeetingAnotherPlayer) {
    BotReport sees_a_slime = Good(0, 0);
    sees_a_slime.max_visible = 3;
    const RunReport report = RunReport::Aggregate({sees_a_slime}, 1, 0);
    EXPECT_EQ(report.saw_another_player, 0U);
    EXPECT_FALSE(report.Succeeded());
}

}  // namespace
}  // namespace sa::loadgen
