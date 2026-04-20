#include "signaling_server.hpp"
#include <rtc/websocket.hpp>
#include <rtc/websocketserver.hpp>
#include <iostream>

SignalingServer::SignalingServer() = default;

SignalingServer::~SignalingServer() { stop(); }

bool SignalingServer::start(uint16_t port) {
    rtc::WebSocketServerConfiguration config;
    config.port = port;
    config.enableTls = false;

    try {
        server_ = std::make_unique<rtc::WebSocketServer>(config);
        port_ = server_->port();
    } catch (const std::exception &e) {
        std::cerr << "SignalingServer: failed to start: " << e.what() << "\n";
        return false;
    }

    server_->onClient([this](std::shared_ptr<rtc::WebSocket> ws) {
        std::cout << "SignalingServer: new client connected\n";

        ws->onMessage([this, ws](rtc::message_variant data) {
            if (std::holds_alternative<rtc::binary>(data)) {
                const auto &bin = std::get<rtc::binary>(data);
                std::string msg(bin.begin(), bin.end());
                handleMessage(msg, ws);
            } else if (std::holds_alternative<std::string>(data)) {
                handleMessage(std::get<std::string>(data), ws);
            }
        });

        ws->onClosed([this, ws]() {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = clients_.begin(); it != clients_.end(); ++it) {
                if (it->second.get() == ws.get()) {
                    std::string id = it->first;
                    clients_.erase(it);
                    if (disconnectHandler_) {
                        disconnectHandler_(id);
                    }
                    break;
                }
            }
        });
    });

    std::cout << "SignalingServer: listening on port " << port_ << "\n";
    return true;
}

void SignalingServer::stop() {
    if (server_) {
        server_->stop();
        server_.reset();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.clear();
}

uint16_t SignalingServer::port() const { return port_; }

void SignalingServer::setConnectRequestHandler(
    std::function<bool(const std::string &)> handler) {
    connectRequestHandler_ = std::move(handler);
}

void SignalingServer::setSdpOfferHandler(
    std::function<void(const std::string &, const std::string &)> handler) {
    sdpOfferHandler_ = std::move(handler);
}

void SignalingServer::setSdpAnswerHandler(
    std::function<void(const std::string &, const std::string &)> handler) {
    sdpAnswerHandler_ = std::move(handler);
}

void SignalingServer::setIceCandidateHandler(
    std::function<void(const std::string &, const std::string &)> handler) {
    iceCandidateHandler_ = std::move(handler);
}

void SignalingServer::setDisconnectHandler(
    std::function<void(const std::string &)> handler) {
    disconnectHandler_ = std::move(handler);
}

void SignalingServer::sendTo(const std::string &instanceId,
                              const sig::SignalingMessage &msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = clients_.find(instanceId);
    if (it != clients_.end()) {
        std::string data = sig::serializeMessage(msg);
        it->second->send(data);
    }
}

void SignalingServer::handleMessage(
    const std::string &data, const std::shared_ptr<rtc::WebSocket> &ws) {
    auto msg = sig::deserializeMessage(data);

    std::cout << "DEBUG: SignalingServer: received message type="
              << sig::messageTypeToString(msg.type) << " from "
              << msg.instanceId << " payload_len=" << msg.payload.size() << "\n" << std::flush;

    switch (msg.type) {
    case sig::MessageType::CONNECT_REQUEST: {
        std::cout << "SignalingServer: connect request from "
                  << msg.instanceId << "\n";

        bool accepted = false;
        if (connectRequestHandler_) {
            accepted = connectRequestHandler_(msg.instanceId);
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (accepted) {
            clients_[msg.instanceId] = ws;
            sig::SignalingMessage acceptMsg;
            acceptMsg.type = sig::MessageType::CONNECT_ACCEPT;
            acceptMsg.instanceId = msg.instanceId;
            ws->send(sig::serializeMessage(acceptMsg));
            std::cout << "SignalingServer: accepted connection from "
                      << msg.instanceId << "\n";
        } else {
            sig::SignalingMessage rejectMsg;
            rejectMsg.type = sig::MessageType::CONNECT_REJECT;
            rejectMsg.instanceId = msg.instanceId;
            rejectMsg.reason = "busy";
            ws->send(sig::serializeMessage(rejectMsg));
            std::cout << "SignalingServer: rejected connection from "
                      << msg.instanceId << " (busy)\n";
        }
        break;
    }
    case sig::MessageType::SDP_OFFER: {
        if (sdpOfferHandler_) {
            sdpOfferHandler_(msg.instanceId, msg.payload);
        }
        break;
    }
    case sig::MessageType::SDP_ANSWER: {
        if (sdpAnswerHandler_) {
            sdpAnswerHandler_(msg.instanceId, msg.payload);
        }
        break;
    }
    case sig::MessageType::ICE_CANDIDATE: {
        if (iceCandidateHandler_) {
            iceCandidateHandler_(msg.instanceId, msg.payload);
        }
        break;
    }
    case sig::MessageType::DISCONNECT: {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            clients_.erase(msg.instanceId);
        }
        if (disconnectHandler_) {
            disconnectHandler_(msg.instanceId);
        }
        break;
    }
    default:
        std::cerr << "SignalingServer: unknown message type\n";
        break;
    }
}