#include "report.hpp"

#include <algorithm>
#include <format>

namespace sa::loadgen {

RunReport RunReport::Aggregate(const std::vector<BotReport>& bots,
                               std::uint32_t min_bots_seeing_others, std::uint32_t min_kills) {
    RunReport out;
    out.min_bots_seeing_others = min_bots_seeing_others;
    out.min_kills = min_kills;
    out.bots = static_cast<std::uint32_t>(bots.size());
    std::vector<std::chrono::milliseconds> rtts;
    for (const BotReport& b : bots) {
        out.provisioned += b.provisioned ? 1U : 0U;
        out.connected += b.connected ? 1U : 0U;
        out.welcomed += b.welcomed ? 1U : 0U;
        out.saw_another_player += b.max_visible > 0 ? 1U : 0U;
        out.failed += b.failure.empty() ? 0U : 1U;
        if (b.kicked) {
            ++out.kicks[game::v1::KickReason_Name(*b.kicked)];
        }
        out.snapshots += b.snapshots;
        out.inputs_sent += b.inputs_sent;
        out.integrity_errors += b.integrity.Total();
        out.casts_accepted += b.combat.casts_accepted;
        out.casts_rejected += b.combat.casts_rejected;
        out.hits_dealt += b.combat.hits_dealt;
        out.kills += b.combat.kills;
        out.deaths += b.combat.deaths;
        out.max_visible = std::max(out.max_visible, b.max_visible);
        rtts.insert(rtts.end(), b.ping_rtts.begin(), b.ping_rtts.end());
    }
    if (!rtts.empty()) {
        std::ranges::sort(rtts);
        out.rtt_p50 = rtts[rtts.size() / 2];
        out.rtt_max = rtts.back();
    }
    return out;
}

bool RunReport::Succeeded() const noexcept {
    const bool everyone_in = welcomed == bots && failed == 0 && kicks.empty();
    const bool enough_met = saw_another_player >= min_bots_seeing_others;
    return everyone_in && enough_met && kills >= min_kills && integrity_errors == 0;
}

std::string RunReport::ToText() const {
    std::string text = std::format(
        "bots={} provisioned={} connected={} welcomed={} saw_another_player={} (required {}) "
        "failed={}\n"
        "snapshots={} inputs_sent={} max_visible={} integrity_errors={}\n",
        bots, provisioned, connected, welcomed, saw_another_player, min_bots_seeing_others, failed,
        snapshots, inputs_sent, max_visible, integrity_errors);
    text += std::format("casts accepted={} rejected={} hits={} kills={} (required {}) deaths={}\n",
                        casts_accepted, casts_rejected, hits_dealt, kills, min_kills, deaths);
    if (rtt_p50 && rtt_max) {
        text += std::format("ping_rtt_ms p50={} max={}\n", rtt_p50->count(), rtt_max->count());
    }
    for (const auto& [reason, count] : kicks) {
        text += std::format("kicked {}: {}\n", reason, count);
    }
    text += Succeeded() ? "RESULT: PASS\n" : "RESULT: FAIL\n";
    return text;
}

}  // namespace sa::loadgen
