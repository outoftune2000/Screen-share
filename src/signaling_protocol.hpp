#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sig {

enum class MessageType : uint8_t {
    CONNECT_REQUEST = 0,
    CONNECT_ACCEPT = 1,
    CONNECT_REJECT = 2,
    SDP_OFFER = 3,
    SDP_ANSWER = 4,
    ICE_CANDIDATE = 5,
    DISCONNECT = 6,
};

inline const char *messageTypeToString(MessageType t) {
    switch (t) {
    case MessageType::CONNECT_REQUEST: return "connect_request";
    case MessageType::CONNECT_ACCEPT: return "connect_accept";
    case MessageType::CONNECT_REJECT: return "connect_reject";
    case MessageType::SDP_OFFER: return "sdp_offer";
    case MessageType::SDP_ANSWER: return "sdp_answer";
    case MessageType::ICE_CANDIDATE: return "ice_candidate";
    case MessageType::DISCONNECT: return "disconnect";
    }
    return "unknown";
}

struct SignalingMessage {
    MessageType type;
    std::string instanceId;
    std::string payload;
    std::string reason;
};

std::string serializeMessage(const SignalingMessage &msg);
SignalingMessage deserializeMessage(const std::string &data);

} // namespace sig