#pragma once

#ifdef __linux__
#include "screen_capture.hpp"
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/xfixes.h>
#include <sys/shm.h>

class XcbScreenCapture : public ScreenCapture {
public:
    XcbScreenCapture();
    ~XcbScreenCapture() override;

    bool initialize() override;
    bool isInitialized() const override;
    std::optional<CapturedFrame> captureFrame() override;

    int width() const override { return screenWidth_; }
    int height() const override { return screenHeight_; }

private:
    bool setupShm();
    void cleanupShm();

    xcb_connection_t *conn_ = nullptr;
    xcb_screen_t *screen_ = nullptr;
    int screenWidth_ = 0;
    int screenHeight_ = 0;

    xcb_shm_seg_t shmSeg_ = 0;
    int shmId_ = -1;
    uint8_t *shmAddr_ = nullptr;
    size_t shmSize_ = 0;
    bool shmReady_ = false;
};

#endif // __linux__