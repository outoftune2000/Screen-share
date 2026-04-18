#include "video_streamer.hpp"
#include "screen_capture_factory.hpp"
#include <rtc/rtc.hpp>
#include <rtc/h264rtppacketizer.hpp>
#include <rtc/rtppacketizationconfig.hpp>
#include <rtc/plihandler.hpp>
#include <chrono>
#include <iostream>

VideoStreamer::VideoStreamer() = default;

VideoStreamer::~VideoStreamer() { stopCapture(); }

bool VideoStreamer::startCapture(int fps, int bitrate) {
    if (running_) return true;

    fps_ = fps;
    bitrate_ = bitrate;

    capture_ = createScreenCapture();
    if (!capture_ || !capture_->initialize()) {
        std::cerr << "VideoStreamer: screen capture init failed\n";
        return false;
    }

    H264Encoder::Config encConfig;
    encConfig.width = capture_->width();
    encConfig.height = capture_->height();
    encConfig.fps = fps_;
    encConfig.bitrate = bitrate_;

    if (!encoder_.initialize(encConfig)) {
        std::cerr << "VideoStreamer: encoder init failed\n";
        return false;
    }

    running_ = true;
    captureThread_ = std::thread(&VideoStreamer::captureLoop, this);
    std::cout << "VideoStreamer: started (" << capture_->width() << "x"
              << capture_->height() << " @ " << fps_ << "fps, "
              << (encoder_.isHardware() ? "VAAPI" : "software") << ")\n";
    return true;
}

void VideoStreamer::stopCapture() {
    running_ = false;
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    encoder_.shutdown();
    if (capture_) {
        capture_->shutdown();
    }
    std::cout << "VideoStreamer: stopped\n";
}

bool VideoStreamer::setupTrack(std::shared_ptr<rtc::PeerConnection> pc) {
    if (!pc) return false;

    rtc::Description::Video media("video",
                                    rtc::Description::Direction::SendOnly);
    media.addH264Codec(96);
    media.addSSRC(1234, "video-send");

    track_ = pc->addTrack(media);

    rtpConfig_ = std::make_shared<rtc::RtpPacketizationConfig>(
        1234, "video-send", 96, 90000);

    auto pkt = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::NalUnit::Separator::Length, rtpConfig_);
    packetizer_ = pkt;
    track_->setMediaHandler(pkt);

    auto pli = std::make_shared<rtc::PliHandler>([this]() {
        std::cout << "VideoStreamer: PLI received, forcing keyframe\n";
    });
    track_->chainMediaHandler(pli);

    std::cout << "VideoStreamer: track set up\n";
    return true;
}

bool VideoStreamer::isRunning() const { return running_; }

void VideoStreamer::setBitrate(int bitrate) {
    std::lock_guard<std::mutex> lock(mutex_);
    bitrate_ = bitrate;
    encoder_.setBitrate(bitrate);
}

void VideoStreamer::captureLoop() {
    auto frameDuration = std::chrono::microseconds(1000000 / fps_);
    auto nextFrame = std::chrono::steady_clock::now();
    uint64_t frameCount = 0;

    while (running_) {
        auto frame = capture_->captureFrame();
        if (!frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        std::vector<EncodedFrame> encodedFrames;
        if (encoder_.encodeFrame(frame->pixels.data(), frame->width,
                                   frame->height, frameCount,
                                   encodedFrames)) {
            onEncodedFrames(encodedFrames);
        }

        frameCount++;
        nextFrame += frameDuration;
        std::this_thread::sleep_until(nextFrame);
    }
}

void VideoStreamer::onEncodedFrames(const std::vector<EncodedFrame> &frames) {
    if (!track_ || !track_->isOpen()) return;

    for (const auto &frame : frames) {
        try {
            track_->send(reinterpret_cast<const rtc::byte *>(frame.data.data()),
                         frame.data.size());
        } catch (const std::exception &e) {
            std::cerr << "VideoStreamer: send error: " << e.what() << "\n";
        }
    }
}