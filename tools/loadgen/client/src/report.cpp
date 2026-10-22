#include "report.hpp"

#include <algorithm>
#include <format>

namespace sa::loadgen {

RunReport RunReport::Aggregate(const std::vector<BotReport>& bots, PassCriteria criteria) {
    RunReport out;
    out.criteria = criteria;
    out.bots = static_cast<std::uint32_t>(bots.size());
    std::vector<std::chrono::milliseconds> rtts;
    for (const BotReport& b : bots) {
        out.provisioned += b.provisioned ? 1U : 0U;
        out.connected += b.connected ? 1U : 0U;
        out.welcomed += b.welcomed ? 1U : 0U;
        out.saw_another_player += b.max_visible_players > 0 ? 1U : 0U;
        out.inputs_acked += b.last_acked_input > 0 ? 1U : 0U;
        out.failed += b.failure.empty() ? 0U : 1U;
        if (b.kicked) {
            ++out.kicks[game::v1::KickReason_Name(*b.kicked)];
        }
        out.snapshots += b.snapshots;
        out.inputs_sent += b.inputs_sent;
        out.casts_sent += b.casts_sent;
        out.integrity_errors += b.integrity.Total();
        out.casts_accepted += b.combat.casts_accepted;
        out.casts_rejected += b.combat.casts_rejected;
        out.casts_unknown += b.combat.casts_unknown;
        out.hits_dealt += b.combat.hits_dealt;
        out.kills += b.combat.kills;
        out.deaths += b.combat.deaths;
        out.pickups_sent += b.pickups_sent;
        out.pickups_acquired += b.loot.pickups_acquired;
        out.pickups_rejected += b.loot.pickups_rejected;
        out.pickups_unknown += b.loot.pickups_unknown;
        out.items_held += b.loot.items_held;
        out.gold += b.loot.gold;
        out.transfers_sent += b.transfers_sent;
        out.transfers_accepted += b.travel.transfers_accepted;
        out.transfers_rejected += b.travel.transfers_rejected;
        out.transfers_unknown += b.travel.transfers_unknown;
        out.zone_changes += b.travel.zone_changes;
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
    // Entering is not enough: every bot must have had its movement processed, so a zone that
    // ignores input fails even if bots happen to spawn next to each other.
    const bool everyone_in =
        welcomed == bots && inputs_acked == bots && failed == 0 && kicks.empty();
    const bool enough_met = saw_another_player >= criteria.min_bots_seeing_others;
    return everyone_in && enough_met && kills >= criteria.min_kills &&
           pickups_acquired >= criteria.min_pickups && zone_changes >= criteria.min_zone_changes &&
           integrity_errors == 0;
}

std::string RunReport::ToText() const {
    std::string text = std::format(
        "bots={} provisioned={} connected={} welcomed={} inputs_acked={} saw_another_player={} "
        "(required {}) failed={}\n"
        "snapshots={} inputs_sent={} max_visible={} integrity_errors={}\n",
        bots, provisioned, connected, welcomed, inputs_acked, saw_another_player,
        criteria.min_bots_seeing_others, failed, snapshots, inputs_sent, max_visible,
        integrity_errors);
    text += std::format(
        "casts sent={} accepted={} rejected={} unknown={} hits={} kills={} (required {}) "
        "deaths={}\n",
        casts_sent, casts_accepted, casts_rejected, casts_unknown, hits_dealt, kills,
        criteria.min_kills, deaths);
    text += std::format(
        "pickups sent={} acquired={} (required {}) rejected={} unknown={} items_held={} "
        "gold={}\n",
        pickups_sent, pickups_acquired, criteria.min_pickups, pickups_rejected, pickups_unknown,
        items_held, gold);
    text += std::format(
        "zone changes={} (required {}) transfers sent={} accepted={} rejected={} unknown={}\n",
        zone_changes, criteria.min_zone_changes, transfers_sent, transfers_accepted,
        transfers_rejected, transfers_unknown);
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
