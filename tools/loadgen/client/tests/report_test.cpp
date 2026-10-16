#include "report.hpp"

#include <gtest/gtest.h>

namespace sa::loadgen {
namespace {

BotReport Good(std::uint32_t index, std::size_t max_visible = 1) {
    BotReport b;
    b.index = index;
    b.provisioned = b.connected = b.welcomed = true;
    b.max_visible = max_visible;
    b.snapshots = 10;
    return b;
}

TEST(RunReport, PassesWhenEveryoneEnteredAndSawSomeone) {
    const RunReport report = RunReport::Aggregate({Good(0), Good(1)}, 2);
    EXPECT_TRUE(report.Succeeded());
    EXPECT_EQ(report.welcomed, 2U);
    EXPECT_EQ(report.saw_another_player, 2U);
}

TEST(RunReport, ThresholdZeroDoesNotRequireAnyMeeting) {
    EXPECT_TRUE(RunReport::Aggregate({Good(0, 0)}, 0).Succeeded());
    EXPECT_TRUE(RunReport::Aggregate({Good(0, 0), Good(1, 1)}, 1).Succeeded());
}

TEST(RunReport, FailsOnKickFailureIsolationOrIntegrity) {
    BotReport kicked = Good(1);
    kicked.kicked = game::v1::KICK_REASON_INVALID_TICKET;
    EXPECT_FALSE(RunReport::Aggregate({Good(0), kicked}, 1).Succeeded());

    BotReport failed = Good(1);
    failed.failure = "connect: refused";
    EXPECT_FALSE(RunReport::Aggregate({Good(0), failed}, 1).Succeeded());

    EXPECT_FALSE(RunReport::Aggregate({Good(0), Good(1, 0)}, 2).Succeeded())
        << "threshold 2, one bot saw nobody";

    BotReport inconsistent = Good(1);
    inconsistent.integrity.despawn_of_unknown = 1;
    const RunReport r = RunReport::Aggregate({Good(0), inconsistent}, 1);
    EXPECT_FALSE(r.Succeeded());
    EXPECT_EQ(r.integrity_errors, 1U);
    EXPECT_NE(r.ToText().find("RESULT: FAIL"), std::string::npos);
}

}  // namespace
}  // namespace sa::loadgen
