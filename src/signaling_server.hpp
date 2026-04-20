#pragma once

#include "discovery.hpp"
#include "signaling_protocol.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace rtc {
class WebSocketServer;
class WebSocket;
}

class SignalingServer {
public:
    SignalingServer();
    ~SignalingServer();

    bool start(uint16_t port = 0);
    void stop();

    uint16_t port() const;

    void setConnectRequestHandler(
        std::function<bool(const std::string &instanceId)> handler);
    void setSdpOfferHandler(
        std::function<void(const std::string &instanceId, const std::string &sdp)> handler);
    void setSdpAnswerHandler(
        std::function<void(const std::string &instanceId, const std::string &sdp)> handler);
    void setIceCandidateHandler(
        std::function<void(const std::string &instanceId, const std::string &candidate)> handler);
    void setDisconnectHandler(
        std::function<void(const std::string &instanceId)> handler);

    void setClientAcceptedHandler(
        std::function<void(const std::string &instanceId)> handler);

    void sendTo(const std::string &instanceId, const sig::SignalingMessage &msg);

private:
    void handleMessage(const std::string &data,
                       const std::shared_ptr<rtc::WebSocket> &ws);

    std::unique_ptr<rtc::WebSocketServer> server_;
    uint16_t port_ = 0;
    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<rtc::WebSocket>> clients_;

    std::function<bool(const std::string &)> connectRequestHandler_;
    std::function<void(const std::string &, const std::string &)> sdpOfferHandler_;
    std::function<void(const std::string &, const std::string &)> sdpAnswerHandler_;
    std::function<void(const std::string &, const std::string &)> iceCandidateHandler_;
    std::function<void(const std::string &)> disconnectHandler_;
    std::function<void(const std::string &)> clientAcceptedHandler_;
};