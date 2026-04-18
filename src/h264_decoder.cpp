#include "h264_decoder.hpp"
#include <iostream>
#include <cstring>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

H264Decoder::H264Decoder() = default;

H264Decoder::~H264Decoder() { shutdown(); }

bool H264Decoder::initialize(bool forceSoftware) {
    if (initialized_) return true;

    if (!forceSoftware) {
        if (initVAAPI()) {
            hwAccel_ = true;
            initialized_ = true;
            std::cout << "H264Decoder: initialized VAAPI\n";
            return true;
        }
        std::cerr << "H264Decoder: VAAPI init failed, falling back to software\n";
    }

    if (initSoftware()) {
        hwAccel_ = false;
        initialized_ = true;
        std::cout << "H264Decoder: initialized software\n";
        return true;
    }

    std::cerr << "H264Decoder: all decoder initializations failed\n";
    return false;
}

bool H264Decoder::initVAAPI() {
    codec_ = avcodec_find_decoder_by_name("h264_vaapi");
    if (!codec_) {
        std::cerr << "H264Decoder: h264_vaapi decoder not found\n";
        return false;
    }

    int ret = av_hwdevice_ctx_create(&hwDeviceCtx_, AV_HWDEVICE_TYPE_VAAPI,
                                       "/dev/dri/renderD128", nullptr, 0);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "H264Decoder: VAAPI device create failed: " << errbuf
                  << "\n";
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec_);
    if (!codecCtx_) {
        av_buffer_unref(&hwDeviceCtx_);
        return false;
    }

    codecCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);
    codecCtx_->codec_id = AV_CODEC_ID_H264;

    ret = avcodec_open2(codecCtx_, codec_, nullptr);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "H264Decoder: VAAPI codec open failed: " << errbuf
                  << "\n";
        avcodec_free_context(&codecCtx_);
        av_buffer_unref(&hwDeviceCtx_);
        return false;
    }

    swFrame_ = av_frame_alloc();
    dstFrame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    return true;
}

bool H264Decoder::initSoftware() {
    codec_ = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec_) {
        std::cerr << "H264Decoder: no H264 decoder found\n";
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec_);
    if (!codecCtx_) return false;

    codecCtx_->codec_id = AV_CODEC_ID_H264;
    codecCtx_->flags2 |= AV_CODEC_FLAG2_FAST;

    int ret = avcodec_open2(codecCtx_, codec_, nullptr);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "H264Decoder: software codec open failed: " << errbuf
                  << "\n";
        avcodec_free_context(&codecCtx_);
        return false;
    }

    swFrame_ = av_frame_alloc();
    dstFrame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    return true;
}

void H264Decoder::shutdown() {
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    if (swFrame_) { av_frame_free(&swFrame_); swFrame_ = nullptr; }
    if (dstFrame_) { av_frame_free(&dstFrame_); dstFrame_ = nullptr; }
    if (packet_) { av_packet_free(&packet_); packet_ = nullptr; }
    if (codecCtx_) { avcodec_free_context(&codecCtx_); codecCtx_ = nullptr; }
    if (hwDeviceCtx_) { av_buffer_unref(&hwDeviceCtx_); hwDeviceCtx_ = nullptr; }
    initialized_ = false;
    codec_ = nullptr;
    dstWidth_ = 0;
    dstHeight_ = 0;
}

bool H264Decoder::isInitialized() const { return initialized_; }

DecodedFrame H264Decoder::frameToRgba(AVFrame *frame) {
    DecodedFrame result;
    result.width = frame->width;
    result.height = frame->height;
    result.pts = frame->pts;

    if (frame->width != dstWidth_ || frame->height != dstHeight_) {
        if (swsCtx_) sws_freeContext(swsCtx_);
        swsCtx_ = sws_getContext(frame->width, frame->height,
                                  AV_PIX_FMT_YUV420P,
                                  frame->width, frame->height,
                                  AV_PIX_FMT_RGBA,
                                  SWS_BILINEAR, nullptr, nullptr, nullptr);
        dstWidth_ = frame->width;
        dstHeight_ = frame->height;

        dstFrame_->format = AV_PIX_FMT_RGBA;
        dstFrame_->width = frame->width;
        dstFrame_->height = frame->height;
        av_frame_get_buffer(dstFrame_, 0);
    }

    sws_scale(swsCtx_, frame->data, frame->linesize, 0, frame->height,
              dstFrame_->data, dstFrame_->linesize);

    result.data.resize(static_cast<size_t>(frame->width) * frame->height * 4);
    for (int y = 0; y < frame->height; y++) {
        memcpy(result.data.data() + y * frame->width * 4,
               dstFrame_->data[0] + y * dstFrame_->linesize[0],
               frame->width * 4);
    }

    return result;
}

std::vector<DecodedFrame> H264Decoder::decode(const uint8_t *data,
                                                size_t size) {
    std::vector<DecodedFrame> results;
    if (!initialized_) return results;

    av_packet_unref(packet_);
    packet_->data = const_cast<uint8_t *>(data);
    packet_->size = static_cast<int>(size);

    int ret = avcodec_send_packet(codecCtx_, packet_);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        return results;
    }

    while (true) {
        AVFrame *targetFrame = swFrame_;
        ret = avcodec_receive_frame(codecCtx_, targetFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        AVFrame *convertFrame = targetFrame;
        if (hwAccel_ && targetFrame->format == AV_PIX_FMT_VAAPI) {
            if (av_hwframe_transfer_data(swFrame_, targetFrame, 0) < 0) {
                av_frame_unref(targetFrame);
                continue;
            }
            convertFrame = swFrame_;
        }

        results.push_back(frameToRgba(convertFrame));
        av_frame_unref(targetFrame);
        if (convertFrame != targetFrame) av_frame_unref(convertFrame);
    }

    return results;
}

std::vector<DecodedFrame> H264Decoder::flush() {
    std::vector<DecodedFrame> results;
    if (!initialized_) return results;

    av_packet_unref(packet_);
    packet_->data = nullptr;
    packet_->size = 0;

    while (true) {
        AVFrame *targetFrame = swFrame_;
        int ret = avcodec_receive_frame(codecCtx_, targetFrame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        AVFrame *convertFrame = targetFrame;
        if (hwAccel_ && targetFrame->format == AV_PIX_FMT_VAAPI) {
            av_hwframe_transfer_data(swFrame_, targetFrame, 0);
            convertFrame = swFrame_;
        }

        results.push_back(frameToRgba(convertFrame));
        av_frame_unref(targetFrame);
        if (convertFrame != targetFrame) av_frame_unref(convertFrame);
    }

    return results;
}