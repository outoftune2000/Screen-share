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

    server.setSdpOfferHandler(
        [this](const std::string &peerId, const std::string &sdp) {
            std::cout << "DEBUG: WebRTC: received SDP offer from " << peerId << "\n" << std::flush;
            peerId_ = peerId;
            if (!pc_) {
                std::cout << "DEBUG: Creating peer connection\n" << std::flush;
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

                pc_->onLocalDescription([this, peerId](rtc::Description desc) {
                    std::cout << "DEBUG: Host: local description created, type=" << desc.typeString() << "\n" << std::flush;
                    sig::SignalingMessage msg;
                    msg.type = sig::MessageType::SDP_ANSWER;
                    msg.instanceId = instanceId_;
                    msg.payload = std::string(desc);
                    server_->sendTo(peerId_, msg);
                    std::cout << "WebRTC: sent SDP answer to " << peerId << "\n" << std::flush;
                });
            }
            if (onTrackSetup_) {
                std::cout << "DEBUG: Calling onTrackSetup handler\n" << std::flush;
                onTrackSetup_(pc_);
            }
            rtc::Description offer(sdp, "offer");
            pc_->setRemoteDescription(offer);
            std::cout << "DEBUG: Host: setRemoteDescription done, calling setLocalDescription\n" << std::flush;
            pc_->setLocalDescription();
            std::cout << "DEBUG: Host: setLocalDescription called\n" << std::flush;
        });

    server.setIceCandidateHandler(
        [this](const std::string &peerId, const std::string &candidate) {
            std::cout << "WebRTC: received ICE candidate from " << peerId
                      << "\n";
            if (pc_) {
                try {
                    pc_->addRemoteCandidate(rtc::Candidate(candidate, "0"));
                } catch (const std::exception &e) {
                    std::cerr << "WebRTC: failed to add ICE candidate: "
                              << e.what() << "\n";
                }
            }
        });

    std::cout << "DEBUG: initAsHost returning true\n";
    return true;
}

bool WebRtcConnection::initAsClient(SignalingClient &client) {
    std::cout << "DEBUG: WebRtcConnection::initAsClient called\n";
    client_ = &client;
    server_ = nullptr;

    setupPeerConnection();

    auto dc = pc_->createDataChannel("input");
    setupDataChannel(dc);
    dc_ = dc;

    pc_->onLocalDescription([this](rtc::Description desc) {
        std::string type = desc.typeString();
        std::cout << "DEBUG: Client: local description created, type=" << type << "\n" << std::flush;

        if (type == "offer") {
            client_->sendSdpOffer(std::string(desc));
            std::cout << "DEBUG: Client: SDP offer sent\n" << std::flush;
        }
    });

    pc_->onLocalCandidate([this](rtc::Candidate candidate) {
        std::cout << "DEBUG: Client: local ICE candidate gathered\n" << std::flush;
        client_->sendIceCandidate(std::string(candidate));
    });

    client.setSdpAnswerHandler(
        [this](const std::string &peerId, const std::string &sdp) {
            std::cout << "WebRTC: received SDP answer\n";
            if (pc_) {
                rtc::Description answer(sdp, "answer");
                pc_->setRemoteDescription(answer);
            }
        });

    client.setIceCandidateHandler(
        [this](const std::string &peerId, const std::string &candidate) {
            if (pc_) {
                try {
                    pc_->addRemoteCandidate(rtc::Candidate(candidate, "0"));
                } catch (const std::exception &e) {
                    std::cerr << "WebRTC: failed to add ICE candidate: "
                              << e.what() << "\n";
                }
            }
        });

    std::cout << "DEBUG: About to setLocalDescription\n";
    pc_->setLocalDescription();
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