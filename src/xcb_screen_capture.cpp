#ifdef __linux__

#include "xcb_screen_capture.hpp"
#include <chrono>
#include <cstring>
#include <iostream>

XcbScreenCapture::XcbScreenCapture() = default;

XcbScreenCapture::~XcbScreenCapture() {
    cleanupShm();
    if (conn_) {
        xcb_disconnect(conn_);
    }
}

bool XcbScreenCapture::initialize() {
    conn_ = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(conn_)) {
        std::cerr << "XcbScreenCapture: failed to connect to X server\n";
        xcb_disconnect(conn_);
        conn_ = nullptr;
        return false;
    }

    const xcb_setup_t *setup = xcb_get_setup(conn_);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    screen_ = iter.data;
    screenWidth_ = screen_->width_in_pixels;
    screenHeight_ = screen_->height_in_pixels;

    std::cout << "XcbScreenCapture: screen " << screenWidth_ << "x"
              << screenHeight_ << "\n";

    if (!setupShm()) {
        std::cerr << "XcbScreenCapture: SHM setup failed, will use fallback\n";
    }

    return true;
}

bool XcbScreenCapture::setupShm() {
    shmSize_ = static_cast<size_t>(screenWidth_) * screenHeight_ * 4;
    shmId_ = shmget(IPC_PRIVATE, shmSize_, IPC_CREAT | 0600);
    if (shmId_ < 0) {
        std::cerr << "XcbScreenCapture: shmget failed\n";
        return false;
    }

    shmAddr_ = static_cast<uint8_t *>(shmat(shmId_, nullptr, 0));
    if (shmAddr_ == reinterpret_cast<uint8_t *>(-1)) {
        std::cerr << "XcbScreenCapture: shmat failed\n";
        shmctl(shmId_, IPC_RMID, nullptr);
        shmId_ = -1;
        shmAddr_ = nullptr;
        return false;
    }

    shmSeg_ = xcb_generate_id(conn_);
    xcb_void_cookie_t cookie = xcb_shm_attach(conn_, shmSeg_,
                                                static_cast<uint32_t>(shmId_), 0);
    xcb_flush(conn_);

    xcb_generic_error_t *error = xcb_request_check(conn_, cookie);
    if (error) {
        std::cerr << "XcbScreenCapture: xcb_shm_attach failed\n";
        free(error);
        shmdt(shmAddr_);
        shmctl(shmId_, IPC_RMID, nullptr);
        shmAddr_ = nullptr;
        shmId_ = -1;
        return false;
    }

    shmReady_ = true;
    return true;
}

void XcbScreenCapture::cleanupShm() {
    if (shmReady_ && conn_) {
        xcb_shm_detach(conn_, shmSeg_);
        xcb_flush(conn_);
    }
    if (shmAddr_) {
        shmdt(shmAddr_);
        shmAddr_ = nullptr;
    }
    if (shmId_ >= 0) {
        shmctl(shmId_, IPC_RMID, nullptr);
        shmId_ = -1;
    }
    shmReady_ = false;
}

bool XcbScreenCapture::isInitialized() const { return conn_ != nullptr; }

std::optional<CapturedFrame> XcbScreenCapture::captureFrame() {
    if (!conn_) return std::nullopt;

    auto startTime = std::chrono::steady_clock::now();

    CapturedFrame frame;
    frame.width = screenWidth_;
    frame.height = screenHeight_;

    if (shmReady_) {
        xcb_shm_get_image_cookie_t cookie = xcb_shm_get_image(
            conn_, screen_->root, 0, 0, screenWidth_, screenHeight_,
            ~0, XCB_IMAGE_FORMAT_Z_PIXMAP, shmSeg_, 0);

        xcb_shm_get_image_reply_t *reply =
            xcb_shm_get_image_reply(conn_, cookie, nullptr);
        if (!reply) {
            std::cerr << "XcbScreenCapture: shm get image failed\n";
            return std::nullopt;
        }
        free(reply);

        frame.pixels.assign(shmAddr_, shmAddr_ + shmSize_);
    } else {
        xcb_get_image_cookie_t cookie = xcb_get_image(
            conn_, XCB_IMAGE_FORMAT_Z_PIXMAP, screen_->root,
            0, 0, screenWidth_, screenHeight_, ~0);

        xcb_get_image_reply_t *reply = xcb_get_image_reply(conn_, cookie, nullptr);
        if (!reply) {
            std::cerr << "XcbScreenCapture: get image failed\n";
            return std::nullopt;
        }

        uint8_t *data = xcb_get_image_data(reply);
        uint32_t dataLen = xcb_get_image_data_length(reply);
        frame.pixels.assign(data, data + dataLen);
        free(reply);
    }

    frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                          startTime.time_since_epoch())
                          .count();

    return frame;
}

#endif // __linux__