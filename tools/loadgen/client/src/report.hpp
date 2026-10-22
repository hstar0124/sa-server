#pragma once

#include "world_model.hpp"

#include <game/server_messages.pb.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sa::loadgen {

// Outcome of one bot, filled in as it runs.
struct BotReport {
    std::uint32_t index = 0;
    std::string username;
    bool provisioned = false;  // account, character and ticket obtained
    bool connected = false;
    bool welcomed = false;
    std::optional<game::v1::KickReason> kicked;
    std::string failure;  // first non-protocol failure (HTTP, socket), empty if none
    std::uint64_t snapshots = 0;
    std::uint64_t inputs_sent = 0;
    std::uint64_t casts_sent = 0;
    std::uint64_t pickups_sent = 0;
    std::uint64_t transfers_sent = 0;  // ChangeZone messages, retries of one request included
    std::uint32_t last_acked_input = 0;
    std::size_t max_visible = 0;
    std::size_t max_visible_players = 0;
    std::vector<std::chrono::milliseconds> ping_rtts;
    WorldModel::Integrity integrity;
    WorldModel::Combat combat;
    WorldModel::Loot loot;
    WorldModel::Travel travel;
};

// What a run must reach to PASS, beyond everyone entering and the world staying consistent.
struct PassCriteria {
    // Random walks on a large map may keep a few bots apart within a short run, so the smoke
    // profile asks for fewer than all.
    std::uint32_t min_bots_seeing_others = 0;
    std::uint32_t min_kills = 0;         // monsters killed across all bots (0 without fighters)
    std::uint32_t min_pickups = 0;       // items acquired across all bots (0 without looters)
    std::uint32_t min_zone_changes = 0;  // zone crossings across all bots (0 without travelers)
};

// Aggregate across bots; the exit code of the run is derived from it.
struct RunReport {
    std::uint32_t bots = 0;
    std::uint32_t provisioned = 0;
    std::uint32_t connected = 0;
    std::uint32_t welcomed = 0;
    std::uint32_t saw_another_player = 0;
    std::uint32_t inputs_acked = 0;  // bots whose movement the zone processed at least once
    std::uint32_t failed = 0;
    std::map<std::string, std::uint32_t> kicks;  // by KickReason name
    std::uint64_t snapshots = 0;
    std::uint64_t inputs_sent = 0;
    std::uint64_t casts_sent = 0;
    std::uint64_t integrity_errors = 0;
    std::size_t max_visible = 0;
    std::optional<std::chrono::milliseconds> rtt_p50;
    std::optional<std::chrono::milliseconds> rtt_max;
    PassCriteria criteria;
    std::uint64_t casts_accepted = 0;
    std::uint64_t casts_rejected = 0;
    std::uint64_t casts_unknown = 0;
    std::uint64_t hits_dealt = 0;
    std::uint64_t kills = 0;
    std::uint64_t deaths = 0;
    std::uint64_t pickups_sent = 0;
    std::uint64_t pickups_acquired = 0;
    std::uint64_t pickups_rejected = 0;
    std::uint64_t pickups_unknown = 0;
    std::uint64_t items_held = 0;  // across all bots at the end of the run
    std::uint64_t gold = 0;
    std::uint64_t transfers_sent = 0;
    std::uint64_t transfers_accepted = 0;
    std::uint64_t transfers_rejected = 0;
    std::uint64_t transfers_unknown = 0;
    std::uint64_t zone_changes = 0;

    [[nodiscard]] static RunReport Aggregate(const std::vector<BotReport>& bots,
                                             PassCriteria criteria);

    // Success: everyone got in, nobody was kicked, the world stayed consistent, and the criteria
    // for meetings, kills, pickups and zone changes were reached.
    [[nodiscard]] bool Succeeded() const noexcept;
    [[nodiscard]] std::string ToText() const;
};

}  // namespace sa::loadgen
