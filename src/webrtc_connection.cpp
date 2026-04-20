#include "webrtc_connection.hpp"
#include <iostream>

WebRtcConnection::WebRtcConnection(const std::string &instanceId,
                                     ConnectionRole role)
    : instanceId_(instanceId), role_(role) {}

WebRtcConnection::~WebRtcConnection() { close(); }

bool WebRtcConnection::initAsHost(SignalingServer &server) {
    std::cout << "DEBUG: WebRtcConnection::initAsHost called\n";
    server_ = &server;
    client_ = nullptr;

    server.setSdpAnswerHandler(
        [this](const std::string &peerId, const std::string &sdp) {
            std::cout << "DEBUG: Host: received SDP answer from " << peerId << "\n" << std::flush;
            if (pc_) {
                rtc::Description answer(sdp, "answer");
                pc_->setRemoteDescription(answer);
                remoteDescriptionSet_ = true;
                for (const auto &c : pendingRemoteCandidates_) {
                    try {
                        pc_->addRemoteCandidate(rtc::Candidate(c, "0"));
                    } catch (const std::exception &e) {
                        std::cerr << "WebRTC: failed to add buffered ICE candidate: "
                                  << e.what() << "\n";
                    }
                }
                pendingRemoteCandidates_.clear();
            }
        });

    server.setIceCandidateHandler(
        [this](const std::string &peerId, const std::string &candidate) {
            std::cout << "DEBUG: Host: received ICE candidate from " << peerId << "\n" << std::flush;
            if (pc_) {
                if (!remoteDescriptionSet_) {
                    std::cout << "DEBUG: Host: buffering ICE candidate\n" << std::flush;
                    pendingRemoteCandidates_.push_back(candidate);
                } else {
                    try {
                        pc_->addRemoteCandidate(rtc::Candidate(candidate, "0"));
                    } catch (const std::exception &e) {
                        std::cerr << "WebRTC: failed to add ICE candidate: "
                                  << e.what() << "\n";
                    }
                }
            }
        });

    std::cout << "DEBUG: initAsHost returning true\n";
    return true;
}

bool WebRtcConnection::startHostSession(const std::string &peerId) {
    std::cout << "DEBUG: WebRtcConnection::startHostSession(" << peerId << ")\n" << std::flush;
    peerId_ = peerId;
    remoteDescriptionSet_ = false;
    pendingRemoteCandidates_.clear();

    if (pc_) {
        pc_->close();
        pc_.reset();
    }

    setupPeerConnection();

    pc_->onLocalCandidate([this](rtc::Candidate candidate) {
        std::cout << "WebRTC: local ICE candidate gathered (host)\n" << std::flush;
        if (server_ && !peerId_.empty()) {
            sig::SignalingMessage msg;
            msg.type = sig::MessageType::ICE_CANDIDATE;
            msg.instanceId = instanceId_;
            msg.payload = std::string(candidate);
            server_->sendTo(peerId_, msg);
        }
    });

    if (onTrackSetup_) {
        std::cout << "DEBUG: Host: adding video track\n" << std::flush;
        onTrackSetup_(pc_);
    }

    auto dc = pc_->createDataChannel("input");
    dc_ = dc;
    setupDataChannel(dc);

    pc_->setLocalDescription();

    auto localDesc = pc_->localDescription();
    if (localDesc) {
        std::cout << "DEBUG: Host: got local description, type=" << localDesc->typeString()
                  << " size=" << std::string(*localDesc).size() << "\n" << std::flush;
        sig::SignalingMessage msg;
        msg.type = sig::MessageType::SDP_OFFER;
        msg.instanceId = instanceId_;
        msg.payload = std::string(*localDesc);
        server_->sendTo(peerId_, msg);
        std::cout << "WebRTC: sent SDP offer to " << peerId << "\n" << std::flush;
    } else {
        std::cout << "DEBUG: Host: localDescription() returned null!\n" << std::flush;
        return false;
    }

    return true;
}

bool WebRtcConnection::initAsClient(SignalingClient &client) {
    std::cout << "DEBUG: WebRtcConnection::initAsClient called\n";
    client_ = &client;
    server_ = nullptr;
    remoteDescriptionSet_ = false;
    pendingRemoteCandidates_.clear();

    setupPeerConnection();

    pc_->onLocalCandidate([this](rtc::Candidate candidate) {
        std::cout << "DEBUG: Client: local ICE candidate gathered\n" << std::flush;
        if (client_) {
            client_->sendIceCandidate(std::string(candidate));
        }
    });

    client.setSdpOfferHandler(
        [this](const std::string &peerId, const std::string &sdp) {
            std::cout << "DEBUG: Client: received SDP offer from " << peerId << "\n" << std::flush;
            if (pc_) {
                rtc::Description offer(sdp, "offer");
                pc_->setRemoteDescription(offer);
                remoteDescriptionSet_ = true;
                for (const auto &c : pendingRemoteCandidates_) {
                    try {
                        pc_->addRemoteCandidate(rtc::Candidate(c, "0"));
                    } catch (const std::exception &e) {
                        std::cerr << "WebRTC: failed to add buffered ICE candidate: "
                                  << e.what() << "\n";
                    }
                }
                pendingRemoteCandidates_.clear();

                pc_->setLocalDescription();

                auto localDesc = pc_->localDescription();
                if (localDesc) {
                    std::cout << "DEBUG: Client: got local description, type=" << localDesc->typeString()
                              << " size=" << std::string(*localDesc).size() << "\n" << std::flush;
                    if (client_) {
                        client_->sendSdpAnswer(std::string(*localDesc));
                        std::cout << "DEBUG: Client: SDP answer sent\n" << std::flush;
                    }
                } else {
                    std::cout << "DEBUG: Client: localDescription() returned null!\n" << std::flush;
                }
            }
        });

    client.setIceCandidateHandler(
        [this](const std::string &peerId, const std::string &candidate) {
            if (pc_) {
                if (!remoteDescriptionSet_) {
                    std::cout << "DEBUG: Client: buffering ICE candidate\n" << std::flush;
                    pendingRemoteCandidates_.push_back(candidate);
                } else {
                    try {
                        pc_->addRemoteCandidate(rtc::Candidate(candidate, "0"));
                    } catch (const std::exception &e) {
                        std::cerr << "WebRTC: failed to add ICE candidate: "
                                  << e.what() << "\n";
                    }
                }
            }
        });

    std::cout << "DEBUG: initAsClient returning true\n";
    return true;
}

void WebRtcConnection::setupPeerConnection() {
    rtc::Configuration config;
    config.iceTransportPolicy = rtc::TransportPolicy::All;
    config.enableIceTcp = false;
    config.enableIceUdpMux = false;

    pc_ = std::make_shared<rtc::PeerConnection>(config);

    pc_->onStateChange([this](rtc::PeerConnection::State state) {
        std::cout << "WebRTC: connection state -> "
                  << static_cast<int>(state) << "\n";
        if (state == rtc::PeerConnection::State::Connected) {
            active_ = true;
        } else if (state == rtc::PeerConnection::State::Disconnected ||
                   state == rtc::PeerConnection::State::Failed ||
                   state == rtc::PeerConnection::State::Closed) {
            active_ = false;
        }
        if (onConnState_) {
            onConnState_(state);
        }
    });

    pc_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> channel) {
        std::cout << "WebRTC: remote DataChannel opened: " << channel->label()
                  << "\n";
        dc_ = channel;
        setupDataChannel(channel);
    });

    pc_->onTrack([this](std::shared_ptr<rtc::Track> track) {
        std::cout << "WebRTC: received video track: " << track->mid() << "\n";
        videoTrack_ = track;
        if (onVideoTrack_) {
            onVideoTrack_(track);
        }
    });
}

void WebRtcConnection::setupDataChannel(
    const std::shared_ptr<rtc::DataChannel> &channel) {
    channel->onOpen([this]() {
        std::cout << "WebRTC: DataChannel '"
                  << (dc_ ? dc_->label() : "?") << "' open\n";
        if (onDcOpen_) {
            onDcOpen_();
        }
    });

    channel->onClosed([this]() {
        std::cout << "WebRTC: DataChannel closed\n";
        if (onDcClose_) {
            onDcClose_();
        }
    });

    channel->onMessage([this](rtc::message_variant data) {
        if (std::holds_alternative<rtc::binary>(data)) {
            const auto &bin = std::get<rtc::binary>(data);
            if (bin.size() >= sizeof(InputEvent)) {
                auto event = InputEvent::deserialize(
                    reinterpret_cast<const uint8_t *>(bin.data()), bin.size());
                if (onInputEvent_) {
                    onInputEvent_(event);
                }
            }
        }
    });
}

void WebRtcConnection::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dc_) {
        dc_->close();
        dc_.reset();
    }
    if (pc_) {
        pc_->close();
        pc_.reset();
    }
    active_ = false;
}

void WebRtcConnection::sendInputEvent(const InputEvent &event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (dc_ && dc_->isOpen()) {
        auto bytes = event.serialize();
        dc_->send(reinterpret_cast<const std::byte *>(bytes.data()),
                   bytes.size());
    }
}

bool WebRtcConnection::isActive() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

ConnectionRole WebRtcConnection::role() const { return role_; }

std::shared_ptr<rtc::PeerConnection> WebRtcConnection::peerConnection() {
    return pc_;
}

void WebRtcConnection::setOnDataChannelOpen(std::function<void()> callback) {
    onDcOpen_ = std::move(callback);
}

void WebRtcConnection::setOnDataChannelClose(
    std::function<void()> callback) {
    onDcClose_ = std::move(callback);
}

void WebRtcConnection::setOnInputEvent(
    std::function<void(const InputEvent &)> callback) {
    onInputEvent_ = std::move(callback);
}

void WebRtcConnection::setOnConnectionStateChange(
    std::function<void(rtc::PeerConnection::State)> callback) {
    onConnState_ = std::move(callback);
}

void WebRtcConnection::setOnVideoTrack(
    std::function<void(std::shared_ptr<rtc::Track>)> callback) {
    onVideoTrack_ = std::move(callback);
}

void WebRtcConnection::setOnTrackSetup(TrackSetupCallback callback) {
    onTrackSetup_ = std::move(callback);
}