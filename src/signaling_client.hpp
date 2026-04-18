#pragma once

#include "signaling_protocol.hpp"
#include <functional>
#include <memory>
#include <string>

namespace rtc {
class WebSocket;
}

class SignalingClient {
public:
    SignalingClient();
    ~SignalingClient();

    bool connect(const std::string &host, uint16_t port,
                 const std::string &instanceId);
    void disconnect();

    bool isConnected() const;

    void sendConnectRequest();
    void sendSdpOffer(const std::string &sdp);
    void sendSdpAnswer(const std::string &sdp);
    void sendIceCandidate(const std::string &candidate);

    void setConnectAcceptHandler(std::function<void()> handler);
    void setConnectRejectHandler(std::function<void(const std::string &reason)> handler);
    void setSdpOfferHandler(
        std::function<void(const std::string &instanceId, const std::string &sdp)> handler);
    void setSdpAnswerHandler(
        std::function<void(const std::string &instanceId, const std::string &sdp)> handler);
    void setIceCandidateHandler(
        std::function<void(const std::string &instanceId, const std::string &candidate)> handler);

private:
    void handleMessage(const std::string &data);

    std::shared_ptr<rtc::WebSocket> ws_;
    std::string instanceId_;
    bool connected_ = false;

    std::function<void()> connectAcceptHandler_;
    std::function<void(const std::string &)> connectRejectHandler_;
    std::function<void(const std::string &, const std::string &)> sdpOfferHandler_;
    std::function<void(const std::string &, const std::string &)> sdpAnswerHandler_;
    std::function<void(const std::string &, const std::string &)> iceCandidateHandler_;
};