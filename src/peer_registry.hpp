#pragma once

#include "discovery.hpp"
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

class PeerRegistry {
public:
    void addOrUpdate(const PeerInfo &peer);

    void remove(const std::string &instanceId);

    std::optional<PeerInfo> get(const std::string &instanceId) const;

    std::vector<PeerInfo> getAll() const;

    size_t size() const;

    void setOnPeerAdded(std::function<void(const PeerInfo &)> cb);
    void setOnPeerRemoved(std::function<void(const std::string &)> cb);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, PeerInfo> peers_;
    std::function<void(const PeerInfo &)> onPeerAdded_;
    std::function<void(const std::string &)> onPeerRemoved_;
};