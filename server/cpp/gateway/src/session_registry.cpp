#include "session_registry.hpp"

#include "client_session.hpp"

#include <spdlog/spdlog.h>

#include <vector>

namespace sa::gateway {

void SessionRegistry::Add(const common::InstanceId& session_id,
                          std::weak_ptr<ClientSession> session) {
    sessions_.insert_or_assign(session_id, std::move(session));
}

void SessionRegistry::Remove(const common::InstanceId& session_id) {
    sessions_.erase(session_id);
}

std::shared_ptr<ClientSession> SessionRegistry::Find(const std::string& wire_id) const {
    const auto id = common::InstanceId::FromString(wire_id);
    if (!id) {
        return nullptr;
    }
    const auto it = sessions_.find(*id);
    return it == sessions_.end() ? nullptr : it->second.lock();
}

void SessionRegistry::CloseAll(game::v1::KickReason reason) {
    // Collect first: closing a session removes it from the registry.
    std::vector<std::shared_ptr<ClientSession>> live;
    for (const auto& [_, weak] : sessions_) {
        if (auto session = weak.lock()) {
            live.push_back(std::move(session));
        }
    }
    for (const auto& session : live) {
        session->Shutdown(reason);
    }
}

void SessionRegistry::OnZoneReady(common::ZoneId zone, const internal::v1::LinkWelcome& welcome) {
    spdlog::info("zone {} ready: world={} hz={}", zone.value(), welcome.world_id(),
                 welcome.simulation_hz());
}

void SessionRegistry::OnZoneDown(common::ZoneId zone) {
    // Collect first: a session's reaction may remove it from the registry.
    std::vector<std::shared_ptr<ClientSession>> affected;
    for (const auto& [_, weak] : sessions_) {
        if (auto session = weak.lock()) {
            affected.push_back(std::move(session));
        }
    }
    for (const auto& session : affected) {
        session->OnZoneLinkDown(zone);
    }
}

void SessionRegistry::OnSessionAttached(common::ZoneId zone,
                                        const internal::v1::SessionAttached& attached) {
    if (auto session = Find(attached.gateway_session_id())) {
        session->OnZoneAttached(zone, attached);
    }
}

void SessionRegistry::OnSessionRejected(common::ZoneId zone,
                                        const internal::v1::SessionRejected& rejected) {
    if (auto session = Find(rejected.gateway_session_id())) {
        session->OnZoneRejected(zone, rejected);
    }
}

void SessionRegistry::OnSessionEvicted(common::ZoneId zone,
                                       const internal::v1::SessionEvicted& evicted) {
    if (auto session = Find(evicted.gateway_session_id())) {
        session->OnZoneEvicted(zone, evicted);
    }
}

void SessionRegistry::OnSessionTransfer(common::ZoneId zone,
                                        const internal::v1::SessionTransfer& transfer) {
    if (auto session = Find(transfer.gateway_session_id())) {
        session->OnZoneTransfer(zone, transfer);
    }
}

void SessionRegistry::OnSessionOutput(common::ZoneId zone,
                                      const internal::v1::SessionOutput& output) {
    if (auto session = Find(output.gateway_session_id())) {
        session->OnZoneOutput(zone, output.message());
    }
}

}  // namespace sa::gateway
