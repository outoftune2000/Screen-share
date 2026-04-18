#include "signaling_protocol.hpp"
#include <cstring>
#include <sstream>

namespace sig {

std::string serializeMessage(const SignalingMessage &msg) {
    std::ostringstream oss;
    oss << static_cast<int>(msg.type) << "|";
    oss << msg.instanceId << "|";
    oss << msg.payload << "|";
    oss << msg.reason;
    return oss.str();
}

SignalingMessage deserializeMessage(const std::string &data) {
    SignalingMessage msg{};
    std::istringstream iss(data);
    std::string token;

    if (std::getline(iss, token, '|')) {
        msg.type = static_cast<MessageType>(std::stoi(token));
    }
    if (std::getline(iss, token, '|')) {
        msg.instanceId = token;
    }
    if (std::getline(iss, token, '|')) {
        msg.payload = token;
    }
    if (std::getline(iss, token, '|')) {
        msg.reason = token;
    }
    return msg;
}

} // namespace sig