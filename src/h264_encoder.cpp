#include "h264_encoder.hpp"
#include <iostream>
#include <cstring>

H264Encoder::H264Encoder() = default;

H264Encoder::~H264Encoder() { shutdown(); }

bool H264Encoder::initialize(const Config &config) {
    if (initialized_) return true;

    targetBitrate_ = config.bitrate;

    if (!config.forceSoftware) {
        if (initVAAPI(config)) {
            hwAccel_ = true;
            initialized_ = true;
            std::cout << "H264Encoder: initialized VAAPI ("
                      << config.width << "x" << config.height << " @ "
                      << config.fps << "fps, " << config.bitrate << "bps)\n";
            return true;
        }
        std::cerr << "H264Encoder: VAAPI init failed, falling back to software\n";
    }

    if (initSoftware(config)) {
        hwAccel_ = false;
        initialized_ = true;
        std::cout << "H264Encoder: initialized software x264 ("
                  << config.width << "x" << config.height << " @ "
                  << config.fps << "fps, " << config.bitrate << "bps)\n";
        return true;
    }

    std::cerr << "H264Encoder: all encoder initializations failed\n";
    return false;
}

bool H264Encoder::initVAAPI(const Config &config) {
    int ret = av_hwdevice_ctx_create(&hwDeviceCtx_, AV_HWDEVICE_TYPE_VAAPI,
                                       config.hwDevice.c_str(), nullptr, 0);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "H264Encoder: VAAPI device create failed: " << errbuf
                  << "\n";
        return false;
    }

    codec_ = avcodec_find_encoder_by_name("h264_vaapi");
    if (!codec_) {
        std::cerr << "H264Encoder: h264_vaapi codec not found\n";
        av_buffer_unref(&hwDeviceCtx_);
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec_);
    if (!codecCtx_) {
        av_buffer_unref(&hwDeviceCtx_);
        return false;
    }

    codecCtx_->width = config.width;
    codecCtx_->height = config.height;
    codecCtx_->time_base = {1, config.fps};
    codecCtx_->framerate = {config.fps, 1};
    codecCtx_->bit_rate = config.bitrate;
    codecCtx_->gop_size = config.gopSize;
    codecCtx_->max_b_frames = 0;
    codecCtx_->pix_fmt = AV_PIX_FMT_VAAPI;
    codecCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);
    codecCtx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codecCtx_->thread_count = 1;

    ret = avcodec_open2(codecCtx_, codec_, nullptr);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "H264Encoder: VAAPI codec open failed: " << errbuf
                  << "\n";
        avcodec_free_context(&codecCtx_);
        av_buffer_unref(&hwDeviceCtx_);
        return false;
    }

    hwFrame_ = av_frame_alloc();
    swFrame_ = av_frame_alloc();
    packet_ = av_packet_alloc();

    return true;
}

bool H264Encoder::initSoftware(const Config &config) {
    codec_ = avcodec_find_encoder_by_name("libx264");
    if (!codec_) {
        codec_ = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    if (!codec_) {
        std::cerr << "H264Encoder: no H264 encoder found\n";
        return false;
    }

    codecCtx_ = avcodec_alloc_context3(codec_);
    if (!codecCtx_) return false;

    codecCtx_->width = config.width;
    codecCtx_->height = config.height;
    codecCtx_->time_base = {1, config.fps};
    codecCtx_->framerate = {config.fps, 1};
    codecCtx_->bit_rate = config.bitrate;
    codecCtx_->gop_size = config.gopSize;
    codecCtx_->max_b_frames = 0;
    codecCtx_->pix_fmt = AV_PIX_FMT_YUV420P;
    codecCtx_->flags |= AV_CODEC_FLAG_LOW_DELAY;

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "preset", "ultrafast", 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);

    int ret = avcodec_open2(codecCtx_, codec_, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "H264Encoder: software codec open failed: " << errbuf
                  << "\n";
        avcodec_free_context(&codecCtx_);
        return false;
    }

    swFrame_ = av_frame_alloc();
    packet_ = av_packet_alloc();

    return true;
}

void H264Encoder::shutdown() {
    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }
    if (hwFrame_) {
        av_frame_free(&hwFrame_);
        hwFrame_ = nullptr;
    }
    if (swFrame_) {
        av_frame_free(&swFrame_);
        swFrame_ = nullptr;
    }
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (hwDeviceCtx_) {
        av_buffer_unref(&hwDeviceCtx_);
        hwDeviceCtx_ = nullptr;
    }
    initialized_ = false;
    codec_ = nullptr;
}

bool H264Encoder::isInitialized() const { return initialized_; }

bool H264Encoder::encodeFrame(const uint8_t *rgbaData, int width, int height,
                               int64_t pts,
                               std::vector<EncodedFrame> &outputFrames) {
    if (!initialized_) return false;

    AVFrame *encodeFrame = swFrame_;

    if (hwAccel_) {
        swFrame_->format = AV_PIX_FMT_RGBA;
        swFrame_->width = width;
        swFrame_->height = height;
        swFrame_->pts = pts;

        if (av_frame_get_buffer(swFrame_, 0) < 0) {
            std::cerr << "H264Encoder: failed to alloc SW frame buffer\n";
            return false;
        }

        // RGBA is packed, single plane
        int srcStride = width * 4;
        int dstStride = swFrame_->linesize[0];
        if (srcStride == dstStride) {
            memcpy(swFrame_->data[0], rgbaData, srcStride * height);
        } else {
            for (int y = 0; y < height; y++) {
                memcpy(swFrame_->data[0] + y * dstStride,
                       rgbaData + y * srcStride, srcStride);
            }
        }

        if (av_hwframe_transfer_data(hwFrame_, swFrame_, 0) < 0) {
            std::cerr << "H264Encoder: HW frame transfer failed\n";
            av_frame_unref(swFrame_);
            return false;
        }

        hwFrame_->pts = pts;
        encodeFrame = hwFrame_;
    } else {
        // Software path: convert RGBA to YUV420P then encode
        swFrame_->format = AV_PIX_FMT_YUV420P;
        swFrame_->width = width;
        swFrame_->height = height;
        swFrame_->pts = pts;

        if (av_frame_get_buffer(swFrame_, 0) < 0) {
            std::cerr << "H264Encoder: failed to alloc frame buffer\n";
            return false;
        }

        // RGBA -> YUV420P conversion
        // Y = 0.299*R + 0.587*G + 0.114*B
        // U = -0.169*R - 0.331*G + 0.500*B + 128
        // V = 0.500*R - 0.419*G - 0.081*B + 128
        int srcStride = width * 4;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int idx = y * srcStride + x * 4;
                uint8_t r = rgbaData[idx];
                uint8_t g = rgbaData[idx + 1];
                uint8_t b = rgbaData[idx + 2];

                swFrame_->data[0][y * swFrame_->linesize[0] + x] =
                    static_cast<uint8_t>(
                        0.299f * r + 0.587f * g + 0.114f * b);

                if (y % 2 == 0 && x % 2 == 0) {
                    int uvIdx = (y / 2) * swFrame_->linesize[1] + (x / 2);
                    swFrame_->data[1][uvIdx] = static_cast<uint8_t>(
                        -0.169f * r - 0.331f * g + 0.500f * b + 128.0f);
                    swFrame_->data[2][uvIdx] = static_cast<uint8_t>(
                        0.500f * r - 0.419f * g - 0.081f * b + 128.0f);
                }
            }
        }
        encodeFrame = swFrame_;
    }

    int ret = avcodec_send_frame(codecCtx_, encodeFrame);
    if (ret < 0) {
        av_frame_unref(swFrame_);
        if (hwFrame_) av_frame_unref(hwFrame_);
        return false;
    }

    while (true) {
        ret = avcodec_receive_packet(codecCtx_, packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            av_frame_unref(swFrame_);
            if (hwFrame_) av_frame_unref(hwFrame_);
            return false;
        }

        EncodedFrame frame;
        frame.data.assign(packet_->data, packet_->data + packet_->size);
        frame.isKeyframe = (packet_->flags & AV_PKT_FLAG_KEY) != 0;
        frame.pts = packet_->pts;
        outputFrames.push_back(std::move(frame));

        av_packet_unref(packet_);
    }

    av_frame_unref(swFrame_);
    if (hwFrame_) av_frame_unref(hwFrame_);
    return true;
}

void H264Encoder::setBitrate(int bitrate) {
    if (!initialized_ || !codecCtx_) return;
    targetBitrate_ = bitrate;
    codecCtx_->bit_rate = bitrate;
}

AVPixelFormat H264Encoder::pixelFormat() const {
    if (!codecCtx_) return AV_PIX_FMT_NONE;
    return codecCtx_->pix_fmt;
}