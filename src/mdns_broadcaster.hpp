#pragma once

#include "discovery.hpp"
#include <functional>
#include <memory>
#include <string>

class MdnsBroadcaster {
public:
    MdnsBroadcaster(const std::string &instanceId, const std::string &hostname,
                    uint16_t signalingPort);
    ~MdnsBroadcaster();

    bool start();
    void stop();

    void updateState(NodeState state);

    void onPeerDiscovered(std::function<void(const PeerInfo &)> callback);
    void onPeerRemoved(std::function<void(const std::string &instanceId)> callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};