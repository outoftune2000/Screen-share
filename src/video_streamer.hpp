#pragma once

#include "h264_encoder.hpp"
#include "screen_capture.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rtc {
class PeerConnection;
class Track;
class H264RtpPacketizer;
class RtpPacketizationConfig;
} // namespace rtc

class VideoStreamer {
public:
    VideoStreamer();
    ~VideoStreamer();

    bool startCapture(int fps = 30, int bitrate = 4'000'000);
    void stopCapture();

    bool setupTrack(std::shared_ptr<rtc::PeerConnection> pc);

    bool isRunning() const;
    void setBitrate(int bitrate);

private:
    void captureLoop();
    void onEncodedFrames(const std::vector<EncodedFrame> &frames);

    std::unique_ptr<ScreenCapture> capture_;
    H264Encoder encoder_;

    std::shared_ptr<rtc::H264RtpPacketizer> packetizer_;
    std::shared_ptr<rtc::RtpPacketizationConfig> rtpConfig_;
    std::shared_ptr<rtc::Track> track_;

    std::thread captureThread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;

    int fps_ = 30;
    int bitrate_ = 4'000'000;
    uint16_t sequenceNumber_ = 0;
    uint32_t timestamp_ = 0;
};