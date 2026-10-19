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
    std::uint32_t last_acked_input = 0;
    std::size_t max_visible = 0;
    std::vector<std::chrono::milliseconds> ping_rtts;
    WorldModel::Integrity integrity;
    WorldModel::Combat combat;
};

// Aggregate across bots; the exit code of the run is derived from it.
struct RunReport {
    std::uint32_t bots = 0;
    std::uint32_t provisioned = 0;
    std::uint32_t connected = 0;
    std::uint32_t welcomed = 0;
    std::uint32_t saw_another_player = 0;
    std::uint32_t failed = 0;
    std::map<std::string, std::uint32_t> kicks;  // by KickReason name
    std::uint64_t snapshots = 0;
    std::uint64_t inputs_sent = 0;
    std::uint64_t casts_sent = 0;
    std::uint64_t integrity_errors = 0;
    std::size_t max_visible = 0;
    std::optional<std::chrono::milliseconds> rtt_p50;
    std::optional<std::chrono::milliseconds> rtt_max;
    std::uint32_t min_bots_seeing_others = 0;
    std::uint32_t min_kills = 0;
    std::uint64_t casts_accepted = 0;
    std::uint64_t casts_rejected = 0;
    std::uint64_t hits_dealt = 0;
    std::uint64_t kills = 0;
    std::uint64_t deaths = 0;

    [[nodiscard]] static RunReport Aggregate(const std::vector<BotReport>& bots,
                                             std::uint32_t min_bots_seeing_others,
                                             std::uint32_t min_kills);

    // Success: everyone got in, nobody was kicked, the world stayed consistent, at least the
    // configured number of bots saw another player, and at least min_kills monsters died.
    [[nodiscard]] bool Succeeded() const noexcept;
    [[nodiscard]] std::string ToText() const;
};

}  // namespace sa::loadgen
