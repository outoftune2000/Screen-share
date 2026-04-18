#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

struct CapturedFrame {
    std::vector<uint8_t> pixels; // RGBA
    int width = 0;
    int height = 0;
    int64_t timestamp = 0; // steady_clock ms
};

class ScreenCapture {
public:
    virtual ~ScreenCapture() = default;

    virtual bool initialize() = 0;
    void shutdown() { running_ = false; }
    virtual bool isInitialized() const = 0;

    virtual std::optional<CapturedFrame> captureFrame() = 0;

    virtual int width() const = 0;
    virtual int height() const = 0;

    void setOnFrameCaptured(
        std::function<void(const CapturedFrame &)> callback) {
        onFrameCaptured_ = std::move(callback);
    }

protected:
    bool running_ = true;
    std::function<void(const CapturedFrame &)> onFrameCaptured_;
};