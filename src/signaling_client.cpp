#include "signaling_client.hpp"
#include <rtc/websocket.hpp>
#include <iostream>

SignalingClient::SignalingClient() = default;

SignalingClient::~SignalingClient() { disconnect(); }

bool SignalingClient::connect(const std::string &host, uint16_t port,
                               const std::string &instanceId) {
    instanceId_ = instanceId;

    rtc::WebSocketConfiguration config;
    config.disableTlsVerification = true;

    ws_ = std::make_shared<rtc::WebSocket>(config);

    ws_->onOpen([this]() {
        std::cout << "SignalingClient: connected to server\n";
        connected_ = true;
        sendConnectRequest();
    });

    ws_->onMessage([this](rtc::message_variant data) {
        if (std::holds_alternative<rtc::binary>(data)) {
            const auto &bin = std::get<rtc::binary>(data);
            handleMessage(std::string(bin.begin(), bin.end()));
        } else if (std::holds_alternative<std::string>(data)) {
            handleMessage(std::get<std::string>(data));
        }
    });

    ws_->onClosed([this]() {
        std::cout << "SignalingClient: disconnected from server\n";
        connected_ = false;
    });

    ws_->onError([this](const std::string &error) {
        std::cerr << "SignalingClient: error: " << error << "\n";
    });

    std::string url = "ws://" + host + ":" + std::to_string(port);
    try {
        ws_->open(url);
    } catch (const std::exception &e) {
        std::cerr << "SignalingClient: failed to connect: " << e.what() << "\n";
        return false;
    }

    std::cout << "SignalingClient: connecting to " << url << "...\n";
    return true;
}

void SignalingClient::disconnect() {
    if (ws_ && connected_) {
        sig::SignalingMessage msg;
        msg.type = sig::MessageType::DISCONNECT;
        msg.instanceId = instanceId_;
        ws_->send(sig::serializeMessage(msg));
    }
    if (ws_) {
        ws_->close();
    }
    connected_ = false;
}

bool SignalingClient::isConnected() const { return connected_; }

void SignalingClient::sendConnectRequest() {
    sig::SignalingMessage msg;
    msg.type = sig::MessageType::CONNECT_REQUEST;
    msg.instanceId = instanceId_;
    ws_->send(sig::serializeMessage(msg));
}

void SignalingClient::sendSdpOffer(const std::string &sdp) {
    std::cout << "DEBUG: SignalingClient::sendSdpOffer size=" << sdp.size() << "\n" << std::flush;
    sig::SignalingMessage msg;
    msg.type = sig::MessageType::SDP_OFFER;
    msg.instanceId = instanceId_;
    msg.payload = sdp;
    ws_->send(sig::serializeMessage(msg));
    std::cout << "DEBUG: SignalingClient::sendSdpOffer sent\n" << std::flush;
}

void SignalingClient::sendSdpAnswer(const std::string &sdp) {
    sig::SignalingMessage msg;
    msg.type = sig::MessageType::SDP_ANSWER;
    msg.instanceId = instanceId_;
    msg.payload = sdp;
    ws_->send(sig::serializeMessage(msg));
}

void SignalingClient::sendIceCandidate(const std::string &candidate) {
    std::cout << "DEBUG: SignalingClient::sendIceCandidate\n" << std::flush;
    sig::SignalingMessage msg;
    msg.type = sig::MessageType::ICE_CANDIDATE;
    msg.instanceId = instanceId_;
    msg.payload = candidate;
    ws_->send(sig::serializeMessage(msg));
}

void SignalingClient::setConnectAcceptHandler(std::function<void()> handler) {
    connectAcceptHandler_ = std::move(handler);
}

void SignalingClient::setConnectRejectHandler(
    std::function<void(const std::string &)> handler) {
    connectRejectHandler_ = std::move(handler);
}

void SignalingClient::setSdpOfferHandler(
    std::function<void(const std::string &, const std::string &)> handler) {
    sdpOfferHandler_ = std::move(handler);
}

void SignalingClient::setSdpAnswerHandler(
    std::function<void(const std::string &, const std::string &)> handler) {
    sdpAnswerHandler_ = std::move(handler);
}

void SignalingClient::setIceCandidateHandler(
    std::function<void(const std::string &, const std::string &)> handler) {
    iceCandidateHandler_ = std::move(handler);
}

void SignalingClient::handleMessage(const std::string &data) {
    auto msg = sig::deserializeMessage(data);

    std::cout << "DEBUG: SignalingClient: received message type="
              << sig::messageTypeToString(msg.type) << " payload_len="
              << msg.payload.size() << "\n" << std::flush;

    switch (msg.type) {
    case sig::MessageType::CONNECT_ACCEPT:
        std::cout << "SignalingClient: connection accepted\n";
        if (connectAcceptHandler_) {
            connectAcceptHandler_();
        }
        break;
    case sig::MessageType::CONNECT_REJECT:
        std::cout << "SignalingClient: connection rejected: " << msg.reason
                  << "\n";
        if (connectRejectHandler_) {
            connectRejectHandler_(msg.reason);
        }
        break;
    case sig::MessageType::SDP_OFFER:
        if (sdpOfferHandler_) {
            sdpOfferHandler_(msg.instanceId, msg.payload);
        }
        break;
    case sig::MessageType::SDP_ANSWER:
        if (sdpAnswerHandler_) {
            sdpAnswerHandler_(msg.instanceId, msg.payload);
        }
        break;
    case sig::MessageType::ICE_CANDIDATE:
        if (iceCandidateHandler_) {
            iceCandidateHandler_(msg.instanceId, msg.payload);
        }
        break;
    default:
        break;
    }
}