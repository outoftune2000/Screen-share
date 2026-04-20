#pragma once

#include "input_protocol.hpp"
#include "signaling_client.hpp"
#include "signaling_server.hpp"
#include "state_machine.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <rtc/rtc.hpp>

enum class ConnectionRole { HOST, CLIENT };

class WebRtcConnection {
public:
    WebRtcConnection(const std::string &instanceId, ConnectionRole role);
    ~WebRtcConnection();

    bool initAsHost(SignalingServer &server);
    bool startHostSession(const std::string &peerId);
    bool initAsClient(SignalingClient &client);

    void close();

    void sendInputEvent(const InputEvent &event);
    std::shared_ptr<rtc::PeerConnection> peerConnection();

    bool isActive() const;
    ConnectionRole role() const;

    void setOnDataChannelOpen(std::function<void()> callback);
    void setOnDataChannelClose(std::function<void()> callback);
    void setOnInputEvent(std::function<void(const InputEvent &)> callback);
    void setOnConnectionStateChange(
        std::function<void(rtc::PeerConnection::State)> callback);
    void setOnVideoTrack(std::function<void(std::shared_ptr<rtc::Track>)> callback);

    using TrackSetupCallback = std::function<bool(std::shared_ptr<rtc::PeerConnection>)>;
    void setOnTrackSetup(TrackSetupCallback callback);

private:
    void setupPeerConnection();
    void setupDataChannel(const std::shared_ptr<rtc::DataChannel> &dc);

    std::string instanceId_;
    std::string peerId_;
    ConnectionRole role_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::DataChannel> dc_;
    std::shared_ptr<rtc::Track> videoTrack_;
    std::vector<std::string> pendingRemoteCandidates_;
    bool remoteDescriptionSet_ = false;
    SignalingServer *server_ = nullptr;
    SignalingClient *client_ = nullptr;
    bool active_ = false;
    mutable std::mutex mutex_;

    std::function<void()> onDcOpen_;
    std::function<void()> onDcClose_;
    std::function<void(const InputEvent &)> onInputEvent_;
    std::function<void(rtc::PeerConnection::State)> onConnState_;
    std::function<void(std::shared_ptr<rtc::Track>)> onVideoTrack_;
    TrackSetupCallback onTrackSetup_;
};