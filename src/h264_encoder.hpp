#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
}

struct EncodedFrame {
    std::vector<uint8_t> data;
    bool isKeyframe = false;
    int64_t pts = 0;
};

class H264Encoder {
public:
    struct Config {
        int width = 1920;
        int height = 1080;
        int fps = 30;
        int bitrate = 4'000'000;
        int gopSize = 30;
        std::string hwDevice = "/dev/dri/renderD128";
        bool forceSoftware = false;
    };

    H264Encoder();
    ~H264Encoder();

    bool initialize(const Config &config);
    void shutdown();
    bool isInitialized() const;

    bool encodeFrame(const uint8_t *rgbaData, int width, int height,
                     int64_t pts, std::vector<EncodedFrame> &outputFrames);

    void setBitrate(int bitrate);
    void requestKeyframe();

    AVPixelFormat pixelFormat() const;
    bool isHardware() const { return hwAccel_; }

private:
    bool initVAAPI(const Config &config);
    bool initSoftware(const Config &config);

    AVBufferRef *hwDeviceCtx_ = nullptr;
    const AVCodec *codec_ = nullptr;
    AVCodecContext *codecCtx_ = nullptr;
    AVFrame *swFrame_ = nullptr;
    AVFrame *hwFrame_ = nullptr;
    AVPacket *packet_ = nullptr;
    bool initialized_ = false;
    bool hwAccel_ = false;
    int targetBitrate_ = 0;
    std::atomic<bool> keyframeRequested_{false};
};