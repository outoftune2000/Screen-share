#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

enum class NodeState : uint8_t { IDLE = 0, BUSY = 1 };

inline const char *nodeStateToString(NodeState s) {
    switch (s) {
    case NodeState::IDLE: return "IDLE";
    case NodeState::BUSY: return "BUSY";
    }
    return "UNKNOWN";
}

struct PeerInfo {
    std::string instanceId;
    std::string hostname;
    NodeState state = NodeState::IDLE;
    std::string address;
    uint16_t signalingPort = 0;
    std::chrono::steady_clock::time_point lastSeen;
};