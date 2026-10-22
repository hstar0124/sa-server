#pragma once

#include "sa/common/ids.hpp"
#include "sa/common/instance_id.hpp"

#include "zone_link.hpp"

#include <memory>
#include <unordered_map>

namespace sa::gateway {

class ClientSession;

// Routes zone-originated messages to the owning ClientSession by gateway_session_id and tells
// every session about zone link state changes.
class SessionRegistry final : public ZoneLink::Handler {
public:
    void Add(const common::InstanceId& session_id, std::weak_ptr<ClientSession> session);
    void Remove(const common::InstanceId& session_id);
    [[nodiscard]] std::size_t size() const noexcept { return sessions_.size(); }
    // Ends every session from the gateway's side, e.g. when its lease is lost and the Backend
    // will hand its characters to other gateways.
    void CloseAll(game::v1::KickReason reason);

    // ZoneLink::Handler
    void OnZoneReady(common::ZoneId zone, const internal::v1::LinkWelcome& welcome) override;
    void OnZoneDown(common::ZoneId zone) override;
    void OnSessionAttached(common::ZoneId zone,
                           const internal::v1::SessionAttached& attached) override;
    void OnSessionRejected(common::ZoneId zone,
                           const internal::v1::SessionRejected& rejected) override;
    void OnSessionEvicted(common::ZoneId zone,
                          const internal::v1::SessionEvicted& evicted) override;
    void OnSessionTransfer(common::ZoneId zone,
                           const internal::v1::SessionTransfer& transfer) override;
    void OnSessionOutput(common::ZoneId zone, const internal::v1::SessionOutput& output) override;

private:
    [[nodiscard]] std::shared_ptr<ClientSession> Find(const std::string& wire_id) const;

    std::unordered_map<common::InstanceId, std::weak_ptr<ClientSession>> sessions_;
};

// zone_id -> link. Topology comes from configuration ([[zones]]).
class ZoneRouter {
public:
    void Add(std::shared_ptr<ZoneLink> link) {
        links_.insert_or_assign(link->zone(), std::move(link));
    }
    [[nodiscard]] ZoneLink* Find(common::ZoneId zone) const {
        const auto it = links_.find(zone);
        return it == links_.end() ? nullptr : it->second.get();
    }
    void StartAll() {
        for (auto& [_, link] : links_) {
            link->Start();
        }
    }
    void StopAll() {
        for (auto& [_, link] : links_) {
            link->Stop();
        }
    }

private:
    std::unordered_map<common::ZoneId, std::shared_ptr<ZoneLink>> links_;
};

}  // namespace sa::gateway
