#include "peer_registry.hpp"

void PeerRegistry::addOrUpdate(const PeerInfo &peer) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(peer.instanceId);
    if (it != peers_.end()) {
        it->second = peer;
        it->second.lastSeen = std::chrono::steady_clock::now();
    } else {
        auto entry = peer;
        entry.lastSeen = std::chrono::steady_clock::now();
        peers_.emplace(peer.instanceId, std::move(entry));
        if (onPeerAdded_) {
            onPeerAdded_(peer);
        }
    }
}

void PeerRegistry::remove(const std::string &instanceId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(instanceId);
    if (it != peers_.end()) {
        if (onPeerRemoved_) {
            onPeerRemoved_(instanceId);
        }
        peers_.erase(it);
    }
}

std::optional<PeerInfo> PeerRegistry::get(const std::string &instanceId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(instanceId);
    if (it != peers_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<PeerInfo> PeerRegistry::getAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PeerInfo> result;
    result.reserve(peers_.size());
    for (const auto &[_, peer] : peers_) {
        result.push_back(peer);
    }
    return result;
}

size_t PeerRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return peers_.size();
}

void PeerRegistry::setOnPeerAdded(std::function<void(const PeerInfo &)> cb) {
    onPeerAdded_ = std::move(cb);
}

void PeerRegistry::setOnPeerRemoved(std::function<void(const std::string &)> cb) {
    onPeerRemoved_ = std::move(cb);
}