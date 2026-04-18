#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
}

struct DecodedFrame {
    std::vector<uint8_t> data; // RGBA pixels
    int width = 0;
    int height = 0;
    int64_t pts = 0;
};

class H264Decoder {
public:
    H264Decoder();
    ~H264Decoder();

    bool initialize(bool forceSoftware = false);
    void shutdown();
    bool isInitialized() const;

    std::vector<DecodedFrame> decode(const uint8_t *data, size_t size);
    std::vector<DecodedFrame> flush();

private:
    bool initVAAPI();
    bool initSoftware();
    DecodedFrame frameToRgba(AVFrame *frame);

    const AVCodec *codec_ = nullptr;
    AVCodecContext *codecCtx_ = nullptr;
    AVFrame *swFrame_ = nullptr;
    AVFrame *dstFrame_ = nullptr;
    AVPacket *packet_ = nullptr;
    AVBufferRef *hwDeviceCtx_ = nullptr;
    struct SwsContext *swsCtx_ = nullptr;

    bool initialized_ = false;
    bool hwAccel_ = false;
    int dstWidth_ = 0;
    int dstHeight_ = 0;
};